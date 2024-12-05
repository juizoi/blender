/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BKE_editmesh.hh"
#include "BKE_modifier.hh"
#include "BKE_object.hh"
#include "BKE_paint.hh"
#include "BKE_paint_bvh.hh"
#include "BKE_particle.h"
#include "BKE_report.hh"
#include "DEG_depsgraph_query.hh"
#include "DNA_fluid_types.h"
#include "ED_paint.hh"
#include "ED_view3d.hh"
#include "GPU_capabilities.hh"
#include "IMB_imbuf_types.hh"

#include "draw_common.hh"
#include "draw_sculpt.hh"

#include "workbench_private.hh"

#include "workbench_engine.h" /* Own include. */

namespace blender::workbench {

using namespace draw;

class Instance {
 private:
  View view_ = {"DefaultView"};

  SceneState scene_state_;

  SceneResources resources_;

  OpaquePass opaque_ps_;
  TransparentPass transparent_ps_;
  TransparentDepthPass transparent_depth_ps_;

  ShadowPass shadow_ps_;
  VolumePass volume_ps_;
  OutlinePass outline_ps_;
  DofPass dof_ps_;
  AntiAliasingPass anti_aliasing_ps_;

  /* An array of nullptr GPUMaterial pointers so we can call DRW_cache_object_surface_material_get.
   * They never get actually used. */
  Vector<GPUMaterial *> dummy_gpu_materials_ = {1, nullptr, {}};

 public:
  GPUMaterial **get_dummy_gpu_materials(int material_count)
  {
    if (material_count > dummy_gpu_materials_.size()) {
      dummy_gpu_materials_.resize(material_count, nullptr);
    }
    return dummy_gpu_materials_.begin();
  };

  void init(Object *camera_ob = nullptr)
  {
    scene_state_.init(camera_ob);
    shadow_ps_.init(scene_state_, resources_);
    resources_.init(scene_state_);

    outline_ps_.init(scene_state_);
    dof_ps_.init(scene_state_);
    anti_aliasing_ps_.init(scene_state_);
  }

  void begin_sync()
  {
    resources_.material_buf.clear_and_trim();

    opaque_ps_.sync(scene_state_, resources_);
    transparent_ps_.sync(scene_state_, resources_);
    transparent_depth_ps_.sync(scene_state_, resources_);

    shadow_ps_.sync();
    volume_ps_.sync(resources_);
    outline_ps_.sync(resources_);
    dof_ps_.sync(resources_);
    anti_aliasing_ps_.sync(scene_state_, resources_);
  }

  void end_sync()
  {
    resources_.material_buf.push_update();
  }

  Material get_material(ObjectRef ob_ref, eV3DShadingColorType color_type, int slot = 0)
  {
    switch (color_type) {
      case V3D_SHADING_OBJECT_COLOR:
        return Material(*ob_ref.object);
      case V3D_SHADING_RANDOM_COLOR:
        return Material(*ob_ref.object, true);
      case V3D_SHADING_SINGLE_COLOR:
        return scene_state_.material_override;
      case V3D_SHADING_VERTEX_COLOR:
        return scene_state_.material_attribute_color;
      case V3D_SHADING_TEXTURE_COLOR:
        ATTR_FALLTHROUGH;
      case V3D_SHADING_MATERIAL_COLOR:
        if (::Material *_mat = BKE_object_material_get_eval(ob_ref.object, slot + 1)) {
          return Material(*_mat);
        }
        ATTR_FALLTHROUGH;
      default:
        return Material(*BKE_material_default_empty());
    }
  }

  void object_sync(Manager &manager, ObjectRef &ob_ref)
  {
    if (scene_state_.render_finished) {
      return;
    }

    Object *ob = ob_ref.object;
    if (!DRW_object_is_renderable(ob)) {
      return;
    }

    const ObjectState object_state = ObjectState(scene_state_, resources_, ob);

    bool is_object_data_visible = (DRW_object_visibility_in_active_context(ob) &
                                   OB_VISIBLE_SELF) &&
                                  (ob->dt >= OB_SOLID || DRW_state_is_scene_render());

    if (!(ob->base_flag & BASE_FROM_DUPLI)) {
      ModifierData *md = BKE_modifiers_findby_type(ob, eModifierType_Fluid);
      if (md && BKE_modifier_is_enabled(scene_state_.scene, md, eModifierMode_Realtime)) {
        FluidModifierData *fmd = (FluidModifierData *)md;
        if (fmd->domain) {
          volume_ps_.object_sync_modifier(manager, resources_, scene_state_, ob_ref, md);

          if (fmd->domain->type == FLUID_DOMAIN_TYPE_GAS) {
            /* Do not draw solid in this case. */
            is_object_data_visible = false;
          }
        }
      }
    }

    ResourceHandle emitter_handle(0);

    if (is_object_data_visible) {
      if (object_state.sculpt_pbvh) {
        const Bounds<float3> bounds = bke::pbvh::bounds_get(
            *bke::object::pbvh_get(*ob_ref.object));
        const float3 center = math::midpoint(bounds.min, bounds.max);
        const float3 half_extent = bounds.max - center;
        ResourceHandle handle = manager.resource_handle(ob_ref, nullptr, &center, &half_extent);
        this->sculpt_sync(ob_ref, handle, object_state);
        emitter_handle = handle;
      }
      else if (ob->type == OB_MESH) {
        ResourceHandle handle = manager.resource_handle(ob_ref);
        this->mesh_sync(ob_ref, handle, object_state);
        emitter_handle = handle;
      }
      else if (ob->type == OB_POINTCLOUD) {
        this->point_cloud_sync(manager, ob_ref, object_state);
      }
      else if (ob->type == OB_CURVES) {
        this->curves_sync(manager, ob_ref, object_state);
      }
      else if (ob->type == OB_VOLUME) {
        if (scene_state_.shading.type != OB_WIRE) {
          volume_ps_.object_sync_volume(manager,
                                        resources_,
                                        scene_state_,
                                        ob_ref,
                                        get_material(ob_ref, object_state.color_type).base_color);
        }
      }
    }

    if (ob->type == OB_MESH && ob->modifiers.first != nullptr) {
      LISTBASE_FOREACH (ModifierData *, md, &ob->modifiers) {
        if (md->type != eModifierType_ParticleSystem) {
          continue;
        }
        ParticleSystem *psys = ((ParticleSystemModifierData *)md)->psys;
        if (!DRW_object_is_visible_psys_in_active_context(ob, psys)) {
          continue;
        }
        ParticleSettings *part = psys->part;
        const int draw_as = (part->draw_as == PART_DRAW_REND) ? part->ren_as : part->draw_as;

        if (draw_as == PART_DRAW_PATH) {
          this->hair_sync(manager, ob_ref, emitter_handle, object_state, psys, md);
        }
      }
    }
  }

  template<typename F>
  void draw_to_mesh_pass(ObjectRef &ob_ref, bool is_transparent, F draw_callback)
  {
    const bool in_front = (ob_ref.object->dtx & OB_DRAW_IN_FRONT) != 0;

    if (scene_state_.xray_mode || is_transparent) {
      if (in_front) {
        draw_callback(transparent_ps_.accumulation_in_front_ps_);
        draw_callback(transparent_depth_ps_.in_front_ps_);
      }
      else {
        draw_callback(transparent_ps_.accumulation_ps_);
        draw_callback(transparent_depth_ps_.main_ps_);
      }
    }
    else {
      if (in_front) {
        draw_callback(opaque_ps_.gbuffer_in_front_ps_);
      }
      else {
        draw_callback(opaque_ps_.gbuffer_ps_);
      }
    }
  }

  void draw_mesh(ObjectRef &ob_ref,
                 Material &material,
                 gpu::Batch *batch,
                 ResourceHandle handle,
                 const MaterialTexture *texture = nullptr,
                 bool show_missing_texture = false)
  {
    resources_.material_buf.append(material);
    int material_index = resources_.material_buf.size() - 1;

    if (show_missing_texture && (!texture || !texture->gpu.texture)) {
      texture = &resources_.missing_texture;
    }

    this->draw_to_mesh_pass(ob_ref, material.is_transparent(), [&](MeshPass &mesh_pass) {
      mesh_pass.get_subpass(eGeometryType::MESH, texture).draw(batch, handle, material_index);
    });
  }

  void mesh_sync(ObjectRef &ob_ref, ResourceHandle handle, const ObjectState &object_state)
  {
    bool has_transparent_material = false;

    if (object_state.use_per_material_batches) {
      const int material_count = DRW_cache_object_material_count_get(ob_ref.object);

      gpu::Batch **batches;
      if (object_state.color_type == V3D_SHADING_TEXTURE_COLOR) {
        batches = DRW_cache_mesh_surface_texpaint_get(ob_ref.object);
      }
      else {
        batches = DRW_cache_object_surface_material_get(
            ob_ref.object, this->get_dummy_gpu_materials(material_count), material_count);
      }

      if (batches) {
        for (auto i : IndexRange(material_count)) {
          if (batches[i] == nullptr) {
            continue;
          }

          int material_slot = i;
          Material mat = this->get_material(ob_ref, object_state.color_type, material_slot);
          has_transparent_material = has_transparent_material || mat.is_transparent();

          MaterialTexture texture;
          if (object_state.color_type == V3D_SHADING_TEXTURE_COLOR) {
            texture = MaterialTexture(ob_ref.object, material_slot);
          }

          this->draw_mesh(
              ob_ref, mat, batches[i], handle, &texture, object_state.show_missing_texture);
        }
      }
    }
    else {
      gpu::Batch *batch;
      if (object_state.color_type == V3D_SHADING_TEXTURE_COLOR) {
        batch = DRW_cache_mesh_surface_texpaint_single_get(ob_ref.object);
      }
      else if (object_state.color_type == V3D_SHADING_VERTEX_COLOR) {
        if (ob_ref.object->mode & OB_MODE_VERTEX_PAINT) {
          batch = DRW_cache_mesh_surface_vertpaint_get(ob_ref.object);
        }
        else {
          batch = DRW_cache_mesh_surface_sculptcolors_get(ob_ref.object);
        }
      }
      else {
        batch = DRW_cache_object_surface_get(ob_ref.object);
      }

      if (batch) {
        Material mat = this->get_material(ob_ref, object_state.color_type);
        has_transparent_material = has_transparent_material || mat.is_transparent();

        this->draw_mesh(ob_ref, mat, batch, handle, &object_state.image_paint_override);
      }
    }

    if (object_state.draw_shadow) {
      shadow_ps_.object_sync(scene_state_, ob_ref, handle, has_transparent_material);
    }
  }

  void sculpt_sync(ObjectRef &ob_ref, ResourceHandle handle, const ObjectState &object_state)
  {
    SculptBatchFeature features = SCULPT_BATCH_DEFAULT;
    if (object_state.color_type == V3D_SHADING_VERTEX_COLOR) {
      features = SCULPT_BATCH_VERTEX_COLOR;
    }
    else if (object_state.color_type == V3D_SHADING_TEXTURE_COLOR) {
      features = SCULPT_BATCH_UV;
    }

    if (object_state.use_per_material_batches) {
      for (SculptBatch &batch : sculpt_batches_get(ob_ref.object, features)) {
        Material mat = this->get_material(ob_ref, object_state.color_type, batch.material_slot);
        if (SCULPT_DEBUG_DRAW) {
          mat.base_color = batch.debug_color();
        }

        MaterialTexture texture;
        if (object_state.color_type == V3D_SHADING_TEXTURE_COLOR) {
          texture = MaterialTexture(ob_ref.object, batch.material_slot);
        }

        this->draw_mesh(
            ob_ref, mat, batch.batch, handle, &texture, object_state.show_missing_texture);
      }
    }
    else {
      Material mat = this->get_material(ob_ref, object_state.color_type);
      for (SculptBatch &batch : sculpt_batches_get(ob_ref.object, features)) {
        if (SCULPT_DEBUG_DRAW) {
          mat.base_color = batch.debug_color();
        }

        this->draw_mesh(ob_ref, mat, batch.batch, handle, &object_state.image_paint_override);
      }
    }
  }

  void point_cloud_sync(Manager &manager, ObjectRef &ob_ref, const ObjectState &object_state)
  {
    ResourceHandle handle = manager.resource_handle(ob_ref);

    Material mat = this->get_material(ob_ref, object_state.color_type);
    resources_.material_buf.append(mat);
    int material_index = resources_.material_buf.size() - 1;

    this->draw_to_mesh_pass(ob_ref, mat.is_transparent(), [&](MeshPass &mesh_pass) {
      PassMain::Sub &pass =
          mesh_pass.get_subpass(eGeometryType::POINTCLOUD).sub("Point Cloud SubPass");
      gpu::Batch *batch = point_cloud_sub_pass_setup(pass, ob_ref.object);
      pass.draw(batch, handle, material_index);
    });
  }

  void hair_sync(Manager &manager,
                 ObjectRef &ob_ref,
                 ResourceHandle emitter_handle,
                 const ObjectState &object_state,
                 ParticleSystem *psys,
                 ModifierData *md)
  {
    /* Skip frustum culling. */
    ResourceHandle handle = manager.resource_handle(ob_ref.object->object_to_world());

    Material mat = this->get_material(ob_ref, object_state.color_type, psys->part->omat - 1);
    MaterialTexture texture;
    if (object_state.color_type == V3D_SHADING_TEXTURE_COLOR) {
      texture = MaterialTexture(ob_ref.object, psys->part->omat - 1);
    }
    resources_.material_buf.append(mat);
    int material_index = resources_.material_buf.size() - 1;

    this->draw_to_mesh_pass(ob_ref, mat.is_transparent(), [&](MeshPass &mesh_pass) {
      PassMain::Sub &pass =
          mesh_pass.get_subpass(eGeometryType::CURVES, &texture).sub("Hair SubPass");
      pass.push_constant("emitter_object_id", int(emitter_handle.raw));
      gpu::Batch *batch = hair_sub_pass_setup(pass, scene_state_.scene, ob_ref.object, psys, md);
      pass.draw(batch, handle, material_index);
    });
  }

  void curves_sync(Manager &manager, ObjectRef &ob_ref, const ObjectState &object_state)
  {
    /* Skip frustum culling. */
    ResourceHandle handle = manager.resource_handle(ob_ref.object->object_to_world());

    Material mat = this->get_material(ob_ref, object_state.color_type);
    resources_.material_buf.append(mat);
    int material_index = resources_.material_buf.size() - 1;

    this->draw_to_mesh_pass(ob_ref, mat.is_transparent(), [&](MeshPass &mesh_pass) {
      PassMain::Sub &pass = mesh_pass.get_subpass(eGeometryType::CURVES).sub("Curves SubPass");
      gpu::Batch *batch = curves_sub_pass_setup(pass, scene_state_.scene, ob_ref.object);
      pass.draw(batch, handle, material_index);
    });
  }

  void draw(Manager &manager,
            GPUTexture *depth_tx,
            GPUTexture *depth_in_front_tx,
            GPUTexture *color_tx)
  {
    int2 resolution = scene_state_.resolution;

    /** Always setup in-front depth, since Overlays can be updated without causing a Workbench
     * re-sync (See #113580). */
    bool needs_depth_in_front = !transparent_ps_.accumulation_in_front_ps_.is_empty() ||
                                (!opaque_ps_.gbuffer_in_front_ps_.is_empty() &&
                                 scene_state_.sample == 0);
    resources_.depth_in_front_tx.wrap(needs_depth_in_front ? depth_in_front_tx : nullptr);
    if (!needs_depth_in_front || opaque_ps_.gbuffer_in_front_ps_.is_empty()) {
      resources_.clear_in_front_fb.ensure(GPU_ATTACHMENT_TEXTURE(depth_in_front_tx));
      resources_.clear_in_front_fb.bind();
      GPU_framebuffer_clear_depth_stencil(resources_.clear_in_front_fb, 1.0f, 0x00);
    }

    resources_.depth_tx.wrap(depth_tx);
    resources_.color_tx.wrap(color_tx);

    if (scene_state_.render_finished) {
      /* Just copy back the already rendered result */
      anti_aliasing_ps_.draw(
          manager, View::default_get(), scene_state_, resources_, depth_in_front_tx);
      return;
    }

    anti_aliasing_ps_.setup_view(view_, scene_state_);

    GPUAttachment id_attachment = GPU_ATTACHMENT_NONE;
    if (scene_state_.draw_object_id) {
      resources_.object_id_tx.acquire(
          resolution, GPU_R16UI, GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
      id_attachment = GPU_ATTACHMENT_TEXTURE(resources_.object_id_tx);
    }
    resources_.clear_fb.ensure(GPU_ATTACHMENT_TEXTURE(resources_.depth_tx),
                               GPU_ATTACHMENT_TEXTURE(resources_.color_tx),
                               id_attachment);
    resources_.clear_fb.bind();
    float4 clear_colors[2] = {scene_state_.background_color, float4(0.0f)};
    GPU_framebuffer_multi_clear(resources_.clear_fb, reinterpret_cast<float(*)[4]>(clear_colors));
    GPU_framebuffer_clear_depth_stencil(resources_.clear_fb, 1.0f, 0x00);

    opaque_ps_.draw(
        manager, view_, resources_, resolution, scene_state_.draw_shadows ? &shadow_ps_ : nullptr);
    transparent_ps_.draw(manager, view_, resources_, resolution);
    transparent_depth_ps_.draw(manager, view_, resources_);

    volume_ps_.draw(manager, view_, resources_);
    outline_ps_.draw(manager, resources_);
    dof_ps_.draw(manager, view_, resources_, resolution);
    anti_aliasing_ps_.draw(manager, view_, scene_state_, resources_, depth_in_front_tx);

    resources_.object_id_tx.release();
  }

  void draw_viewport(Manager &manager,
                     GPUTexture *depth_tx,
                     GPUTexture *depth_in_front_tx,
                     GPUTexture *color_tx)
  {
    this->draw(manager, depth_tx, depth_in_front_tx, color_tx);

    if (scene_state_.sample + 1 < scene_state_.samples_len) {
      DRW_viewport_request_redraw();
    }
  }

  void draw_image_render(Manager &manager,
                         GPUTexture *depth_tx,
                         GPUTexture *depth_in_front_tx,
                         GPUTexture *color_tx,
                         RenderEngine *engine = nullptr)
  {
    BLI_assert(scene_state_.sample == 0);
    for (auto i : IndexRange(scene_state_.samples_len)) {
      if (engine && RE_engine_test_break(engine)) {
        break;
      }
      if (i != 0) {
        scene_state_.sample = i;
        /* Re-sync anything dependent on scene_state.sample. */
        resources_.init(scene_state_);
        dof_ps_.init(scene_state_);
        anti_aliasing_ps_.sync(scene_state_, resources_);
      }
      this->draw(manager, depth_tx, depth_in_front_tx, color_tx);
      /* Perform render step between samples to allow
       * flushing of freed GPUBackend resources. */
      if (GPU_backend_get_type() == GPU_BACKEND_METAL) {
        GPU_flush();
      }
      GPU_render_step();
    }
  }

  void reset_taa_sample()
  {
    scene_state_.reset_taa_next_sample = true;
  }
};

}  // namespace blender::workbench

/* -------------------------------------------------------------------- */
/** \name Interface with legacy C DRW manager
 * \{ */

using namespace blender;

struct WORKBENCH_Data {
  DrawEngineType *engine_type;
  DRWViewportEmptyList *fbl;
  DRWViewportEmptyList *txl;
  DRWViewportEmptyList *psl;
  DRWViewportEmptyList *stl;
  workbench::Instance *instance;

  char info[GPU_INFO_SIZE];
};

static void workbench_engine_init(void *vedata)
{
  WORKBENCH_Data *ved = reinterpret_cast<WORKBENCH_Data *>(vedata);
  if (ved->instance == nullptr) {
    ved->instance = new workbench::Instance();
  }

  ved->instance->init();
}

static void workbench_cache_init(void *vedata)
{
  reinterpret_cast<WORKBENCH_Data *>(vedata)->instance->begin_sync();
}

static void workbench_cache_populate(void *vedata, Object *object)
{
  draw::Manager *manager = DRW_manager_get();

  draw::ObjectRef ref;
  ref.object = object;
  ref.dupli_object = DRW_object_get_dupli(object);
  ref.dupli_parent = DRW_object_get_dupli_parent(object);
  ref.handle = draw::ResourceHandle(0);

  reinterpret_cast<WORKBENCH_Data *>(vedata)->instance->object_sync(*manager, ref);
}

static void workbench_cache_finish(void *vedata)
{
  reinterpret_cast<WORKBENCH_Data *>(vedata)->instance->end_sync();
}

static void workbench_draw_scene(void *vedata)
{
  WORKBENCH_Data *ved = reinterpret_cast<WORKBENCH_Data *>(vedata);
  DefaultTextureList *dtxl = DRW_viewport_texture_list_get();
  draw::Manager *manager = DRW_manager_get();
  if (DRW_state_is_viewport_image_render()) {
    ved->instance->draw_image_render(*manager, dtxl->depth, dtxl->depth_in_front, dtxl->color);
  }
  else {
    ved->instance->draw_viewport(*manager, dtxl->depth, dtxl->depth_in_front, dtxl->color);
  }
}

static void workbench_instance_free(void *instance)
{
  delete reinterpret_cast<workbench::Instance *>(instance);
}

static void workbench_engine_free()
{
  workbench::ShaderCache::release();
}

static void workbench_view_update(void *vedata)
{
  WORKBENCH_Data *ved = reinterpret_cast<WORKBENCH_Data *>(vedata);
  if (ved->instance) {
    ved->instance->reset_taa_sample();
  }
}

static void workbench_id_update(void *vedata, ID *id)
{
  UNUSED_VARS(vedata, id);
}

/* RENDER */

static bool workbench_render_framebuffers_init()
{
  /* For image render, allocate own buffers because we don't have a viewport. */
  const float2 viewport_size = DRW_viewport_size_get();
  const int2 size = {int(viewport_size.x), int(viewport_size.y)};

  DefaultTextureList *dtxl = DRW_viewport_texture_list_get();

  /* When doing a multi view rendering the first view will allocate the buffers
   * the other views will reuse these buffers */
  if (dtxl->color == nullptr) {
    BLI_assert(dtxl->depth == nullptr);
    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_GENERAL;
    dtxl->color = GPU_texture_create_2d(
        "txl.color", size.x, size.y, 1, GPU_RGBA16F, usage, nullptr);
    dtxl->depth = GPU_texture_create_2d(
        "txl.depth", size.x, size.y, 1, GPU_DEPTH24_STENCIL8, usage, nullptr);
    dtxl->depth_in_front = GPU_texture_create_2d(
        "txl.depth_in_front", size.x, size.y, 1, GPU_DEPTH24_STENCIL8, usage, nullptr);
  }

  if (!(dtxl->depth && dtxl->color && dtxl->depth_in_front)) {
    return false;
  }

  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();

  GPU_framebuffer_ensure_config(
      &dfbl->default_fb,
      {GPU_ATTACHMENT_TEXTURE(dtxl->depth), GPU_ATTACHMENT_TEXTURE(dtxl->color)});

  GPU_framebuffer_ensure_config(&dfbl->depth_only_fb,
                                {GPU_ATTACHMENT_TEXTURE(dtxl->depth), GPU_ATTACHMENT_NONE});

  GPU_framebuffer_ensure_config(&dfbl->color_only_fb,
                                {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(dtxl->color)});

  return GPU_framebuffer_check_valid(dfbl->default_fb, nullptr) &&
         GPU_framebuffer_check_valid(dfbl->color_only_fb, nullptr) &&
         GPU_framebuffer_check_valid(dfbl->depth_only_fb, nullptr);
}

static void write_render_color_output(RenderLayer *layer,
                                      const char *viewname,
                                      GPUFrameBuffer *fb,
                                      const rcti *rect)
{
  RenderPass *rp = RE_pass_find_by_name(layer, RE_PASSNAME_COMBINED, viewname);
  if (rp) {
    GPU_framebuffer_bind(fb);
    GPU_framebuffer_read_color(fb,
                               rect->xmin,
                               rect->ymin,
                               BLI_rcti_size_x(rect),
                               BLI_rcti_size_y(rect),
                               4,
                               0,
                               GPU_DATA_FLOAT,
                               rp->ibuf->float_buffer.data);
  }
}

static void write_render_z_output(RenderLayer *layer,
                                  const char *viewname,
                                  GPUFrameBuffer *fb,
                                  const rcti *rect,
                                  const float4x4 &winmat)
{
  RenderPass *rp = RE_pass_find_by_name(layer, RE_PASSNAME_Z, viewname);
  if (rp) {
    GPU_framebuffer_bind(fb);
    GPU_framebuffer_read_depth(fb,
                               rect->xmin,
                               rect->ymin,
                               BLI_rcti_size_x(rect),
                               BLI_rcti_size_y(rect),
                               GPU_DATA_FLOAT,
                               rp->ibuf->float_buffer.data);

    int pix_num = BLI_rcti_size_x(rect) * BLI_rcti_size_y(rect);

    /* Convert GPU depth [0..1] to view Z [near..far] */
    if (blender::draw::View::default_get().is_persp()) {
      for (float &z : MutableSpan(rp->ibuf->float_buffer.data, pix_num)) {
        if (z == 1.0f) {
          z = 1e10f; /* Background */
        }
        else {
          z = z * 2.0f - 1.0f;
          z = winmat[3][2] / (z + winmat[2][2]);
        }
      }
    }
    else {
      /* Keep in mind, near and far distance are negatives. */
      float near = blender::draw::View::default_get().near_clip();
      float far = blender::draw::View::default_get().far_clip();
      float range = fabsf(far - near);

      for (float &z : MutableSpan(rp->ibuf->float_buffer.data, pix_num)) {
        if (z == 1.0f) {
          z = 1e10f; /* Background */
        }
        else {
          z = z * range - near;
        }
      }
    }
  }
}

static void workbench_render_to_image(void *vedata,
                                      RenderEngine *engine,
                                      RenderLayer *layer,
                                      const rcti *rect)
{
  using namespace blender::draw;
  if (!workbench_render_framebuffers_init()) {
    RE_engine_report(engine, RPT_ERROR, "Failed to allocate GPU buffers");
    return;
  }

  /* Setup */
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();
  const DRWContextState *draw_ctx = DRW_context_state_get();
  Depsgraph *depsgraph = draw_ctx->depsgraph;

  WORKBENCH_Data *ved = reinterpret_cast<WORKBENCH_Data *>(vedata);
  if (ved->instance == nullptr) {
    ved->instance = new workbench::Instance();
  }

  /* TODO(sergey): Shall render hold pointer to an evaluated camera instead? */
  Object *camera_ob = DEG_get_evaluated_object(depsgraph, RE_GetCamera(engine->re));

  /* Set the perspective, view and window matrix. */
  float4x4 winmat, viewmat, viewinv;
  RE_GetCameraWindow(engine->re, camera_ob, winmat.ptr());
  RE_GetCameraModelMatrix(engine->re, camera_ob, viewinv.ptr());
  viewmat = math::invert(viewinv);

  /* Render */
  /* TODO: Remove old draw manager calls. */
  DRW_cache_restart();
  blender::draw::View::default_set(float4x4(viewmat), float4x4(winmat));

  ved->instance->init(camera_ob);

  draw::Manager &manager = *DRW_manager_get();
  manager.begin_sync();

  workbench_cache_init(vedata);
  auto workbench_render_cache =
      [](void *vedata, Object *ob, RenderEngine * /*engine*/, Depsgraph * /*depsgraph*/) {
        workbench_cache_populate(vedata, ob);
      };
  DRW_render_object_iter(vedata, engine, depsgraph, workbench_render_cache);
  workbench_cache_finish(vedata);

  manager.end_sync();

  /* TODO: Remove old draw manager calls. */
  DRW_curves_update(manager);

  DefaultTextureList &dtxl = *DRW_viewport_texture_list_get();
  ved->instance->draw_image_render(manager, dtxl.depth, dtxl.depth_in_front, dtxl.color, engine);

  /* Write image */
  const char *viewname = RE_GetActiveRenderView(engine->re);
  write_render_color_output(layer, viewname, dfbl->default_fb, rect);
  write_render_z_output(layer, viewname, dfbl->default_fb, rect, winmat);
}

static void workbench_render_update_passes(RenderEngine *engine,
                                           Scene *scene,
                                           ViewLayer *view_layer)
{
  if (view_layer->passflag & SCE_PASS_COMBINED) {
    RE_engine_register_pass(engine, scene, view_layer, RE_PASSNAME_COMBINED, 4, "RGBA", SOCK_RGBA);
  }
  if (view_layer->passflag & SCE_PASS_Z) {
    RE_engine_register_pass(engine, scene, view_layer, RE_PASSNAME_Z, 1, "Z", SOCK_FLOAT);
  }
}

extern "C" {

static const DrawEngineDataSize workbench_data_size = DRW_VIEWPORT_DATA_SIZE(WORKBENCH_Data);

DrawEngineType draw_engine_workbench = {
    /*next*/ nullptr,
    /*prev*/ nullptr,
    /*idname*/ N_("Workbench"),
    /*vedata_size*/ &workbench_data_size,
    /*engine_init*/ &workbench_engine_init,
    /*engine_free*/ &workbench_engine_free,
    /*instance_free*/ &workbench_instance_free,
    /*cache_init*/ &workbench_cache_init,
    /*cache_populate*/ &workbench_cache_populate,
    /*cache_finish*/ &workbench_cache_finish,
    /*draw_scene*/ &workbench_draw_scene,
    /*view_update*/ &workbench_view_update,
    /*id_update*/ &workbench_id_update,
    /*render_to_image*/ &workbench_render_to_image,
    /*store_metadata*/ nullptr,
};

RenderEngineType DRW_engine_viewport_workbench_type = {
    /*next*/ nullptr,
    /*prev*/ nullptr,
    /*idname*/ "BLENDER_WORKBENCH",
    /*name*/ N_("Workbench"),
    /*flag*/ RE_INTERNAL | RE_USE_STEREO_VIEWPORT | RE_USE_GPU_CONTEXT,
    /*update*/ nullptr,
    /*render*/ &DRW_render_to_image,
    /*render_frame_finish*/ nullptr,
    /*draw*/ nullptr,
    /*bake*/ nullptr,
    /*view_update*/ nullptr,
    /*view_draw*/ nullptr,
    /*update_script_node*/ nullptr,
    /*update_render_passes*/ &workbench_render_update_passes,
    /*draw_engine*/ &draw_engine_workbench,
    /*rna_ext*/
    {
        /*data*/ nullptr,
        /*srna*/ nullptr,
        /*call*/ nullptr,
    },
};
}

/** \} */
