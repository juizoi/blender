/* SPDX-FileCopyrightText: 2019 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup editor/io
 */

#ifdef WITH_USD
#  include "DNA_modifier_types.h"
#  include "DNA_space_types.h"

#  include "BKE_context.hh"
#  include "BKE_file_handler.hh"
#  include "BKE_report.hh"

#  include "BLI_path_utils.hh"
#  include "BLI_string.h"

#  include "BLT_translation.hh"

#  include "ED_fileselect.hh"
#  include "ED_object.hh"

#  include "MEM_guardedalloc.h"

#  include "RNA_access.hh"
#  include "RNA_define.hh"
#  include "RNA_enum_types.hh"

#  include "UI_interface.hh"
#  include "UI_resources.hh"

#  include "WM_api.hh"
#  include "WM_types.hh"

#  include "DEG_depsgraph.hh"

#  include "IO_orientation.hh"
#  include "io_usd.hh"
#  include "io_utils.hh"
#  include "usd.hh"

#  include <pxr/pxr.h>

#  include <string>
#  include <utility>

using namespace blender::io::usd;

const EnumPropertyItem rna_enum_usd_export_evaluation_mode_items[] = {
    {DAG_EVAL_RENDER,
     "RENDER",
     0,
     "Render",
     "Use Render settings for object visibility, modifier settings, etc"},
    {DAG_EVAL_VIEWPORT,
     "VIEWPORT",
     0,
     "Viewport",
     "Use Viewport settings for object visibility, modifier settings, etc"},
    {0, nullptr, 0, nullptr, nullptr},
};

const EnumPropertyItem rna_enum_usd_mtl_name_collision_mode_items[] = {
    {USD_MTL_NAME_COLLISION_MAKE_UNIQUE,
     "MAKE_UNIQUE",
     0,
     "Make Unique",
     "Import each USD material as a unique Blender material"},
    {USD_MTL_NAME_COLLISION_REFERENCE_EXISTING,
     "REFERENCE_EXISTING",
     0,
     "Reference Existing",
     "If a material with the same name already exists, reference that instead of importing"},
    {0, nullptr, 0, nullptr, nullptr},
};

const EnumPropertyItem rna_enum_usd_attr_import_mode_items[] = {
    {USD_ATTR_IMPORT_NONE, "NONE", 0, "None", "Do not import USD custom attributes"},
    {USD_ATTR_IMPORT_USER,
     "USER",
     0,
     "User",
     "Import USD attributes in the 'userProperties' namespace as Blender custom "
     "properties. The namespace will be stripped from the property names"},
    {USD_ATTR_IMPORT_ALL,
     "ALL",
     0,
     "All Custom",
     "Import all USD custom attributes as Blender custom properties. "
     "Namespaces will be retained in the property names"},
    {0, nullptr, 0, nullptr, nullptr},
};

const EnumPropertyItem rna_enum_usd_tex_import_mode_items[] = {
    {USD_TEX_IMPORT_NONE, "IMPORT_NONE", 0, "None", "Don't import textures"},
    {USD_TEX_IMPORT_PACK, "IMPORT_PACK", 0, "Packed", "Import textures as packed data"},
    {USD_TEX_IMPORT_COPY, "IMPORT_COPY", 0, "Copy", "Copy files to textures directory"},
    {0, nullptr, 0, nullptr, nullptr},
};

const EnumPropertyItem rna_enum_usd_tex_name_collision_mode_items[] = {
    {USD_TEX_NAME_COLLISION_USE_EXISTING,
     "USE_EXISTING",
     0,
     "Use Existing",
     "If a file with the same name already exists, use that instead of copying"},
    {USD_TEX_NAME_COLLISION_OVERWRITE, "OVERWRITE", 0, "Overwrite", "Overwrite existing files"},
    {0, nullptr, 0, nullptr, nullptr},
};

const EnumPropertyItem rna_enum_usd_export_subdiv_mode_items[] = {
    {USD_SUBDIV_IGNORE,
     "IGNORE",
     0,
     "Ignore",
     "Scheme = None. Export base mesh without subdivision"},
    {USD_SUBDIV_TESSELLATE,
     "TESSELLATE",
     0,
     "Tessellate",
     "Scheme = None. Export subdivided mesh"},
    {USD_SUBDIV_BEST_MATCH,
     "BEST_MATCH",
     0,
     "Best Match",
     "Scheme = Catmull-Clark, when possible. "
     "Reverts to exporting the subdivided mesh for the Simple subdivision type"},
    {0, nullptr, 0, nullptr, nullptr},
};

const EnumPropertyItem rna_enum_usd_xform_op_mode_items[] = {
    {USD_XFORM_OP_TRS,
     "TRS",
     0,
     "Translate, Rotate, Scale",
     "Export with translate, rotate, and scale Xform operators"},
    {USD_XFORM_OP_TOS,
     "TOS",
     0,
     "Translate, Orient, Scale",
     "Export with translate, orient quaternion, and scale Xform operators"},
    {USD_XFORM_OP_MAT, "MAT", 0, "Matrix", "Export matrix operator"},
    {0, nullptr, 0, nullptr, nullptr},
};

const EnumPropertyItem prop_usdz_downscale_size[] = {
    {USD_TEXTURE_SIZE_KEEP, "KEEP", 0, "Keep", "Keep all current texture sizes"},
    {USD_TEXTURE_SIZE_256, "256", 0, "256", "Resize to a maximum of 256 pixels"},
    {USD_TEXTURE_SIZE_512, "512", 0, "512", "Resize to a maximum of 512 pixels"},
    {USD_TEXTURE_SIZE_1024, "1024", 0, "1024", "Resize to a maximum of 1024 pixels"},
    {USD_TEXTURE_SIZE_2048, "2048", 0, "2048", "Resize to a maximum of 2048 pixels"},
    {USD_TEXTURE_SIZE_4096, "4096", 0, "4096", "Resize to a maximum of 4096 pixels"},
    {USD_TEXTURE_SIZE_CUSTOM, "CUSTOM", 0, "Custom", "Specify a custom size"},
    {0, nullptr, 0, nullptr, nullptr},
};

const EnumPropertyItem rna_enum_usd_tex_export_mode_items[] = {
    {USD_TEX_EXPORT_KEEP, "KEEP", 0, "Keep", "Use original location of textures"},
    {USD_TEX_EXPORT_PRESERVE,
     "PRESERVE",
     0,
     "Preserve",
     "Preserve file paths of textures from already imported USD files.\n"
     "Export remaining textures to a 'textures' folder next to the USD file"},
    {USD_TEX_EXPORT_NEW_PATH,
     "NEW",
     0,
     "New Path",
     "Export textures to a 'textures' folder next to the USD file"},
    {0, nullptr, 0, nullptr, nullptr}};

const EnumPropertyItem rna_enum_usd_mtl_purpose_items[] = {
    {USD_MTL_PURPOSE_ALL,
     "MTL_ALL_PURPOSE",
     0,
     "All Purpose",
     "Attempt to import 'allPurpose' materials."},
    {USD_MTL_PURPOSE_PREVIEW,
     "MTL_PREVIEW",
     0,
     "Preview",
     "Attempt to import 'preview' materials. "
     "Load 'allPurpose' materials as a fallback"},
    {USD_MTL_PURPOSE_FULL,
     "MTL_FULL",
     0,
     "Full",
     "Attempt to import 'full' materials. "
     "Load 'allPurpose' or 'preview' materials, in that order, as a fallback"},
    {0, nullptr, 0, nullptr, nullptr},
};

/* Stored in the wmOperator's customdata field to indicate it should run as a background job.
 * This is set when the operator is invoked, and not set when it is only executed. */
struct eUSDOperatorOptions {
  bool as_background_job;
};

/* Ensure that the prim_path is not set to
 * the absolute root path '/'. */
static void process_prim_path(char *prim_path)
{
  if (prim_path == nullptr || prim_path[0] == '\0') {
    return;
  }

  /* The absolute root "/" path indicates a no-op,
   * so clear the string. */
  if (prim_path[0] == '/' && prim_path[1] == '\0') {
    prim_path[0] = '\0';
  }

  /* If a prim path doesn't start with a "/" it
   * is invalid when creating the prim. */
  if (prim_path[0] != '/') {
    const std::string prim_path_copy = std::string(prim_path);
    BLI_snprintf(prim_path, FILE_MAX, "/%s", prim_path_copy.c_str());
  }
}

static int wm_usd_export_invoke(bContext *C, wmOperator *op, const wmEvent * /*event*/)
{
  eUSDOperatorOptions *options = MEM_cnew<eUSDOperatorOptions>("eUSDOperatorOptions");
  options->as_background_job = true;
  op->customdata = options;

  ED_fileselect_ensure_default_filepath(C, op, ".usdc");

  WM_event_add_fileselect(C, op);

  return OPERATOR_RUNNING_MODAL;
}

static int wm_usd_export_exec(bContext *C, wmOperator *op)
{
  if (!RNA_struct_property_is_set_ex(op->ptr, "filepath", false)) {
    BKE_report(op->reports, RPT_ERROR, "No filepath given");
    return OPERATOR_CANCELLED;
  }

  char filepath[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filepath);

  eUSDOperatorOptions *options = static_cast<eUSDOperatorOptions *>(op->customdata);
  const bool as_background_job = (options != nullptr && options->as_background_job);
  MEM_SAFE_FREE(op->customdata);

  const bool selected_objects_only = RNA_boolean_get(op->ptr, "selected_objects_only");
  const bool visible_objects_only = RNA_boolean_get(op->ptr, "visible_objects_only");
  const bool export_animation = RNA_boolean_get(op->ptr, "export_animation");
  const bool export_hair = RNA_boolean_get(op->ptr, "export_hair");
  const bool export_uvmaps = RNA_boolean_get(op->ptr, "export_uvmaps");
  const bool rename_uvmaps = RNA_boolean_get(op->ptr, "rename_uvmaps");
  const bool export_mesh_colors = RNA_boolean_get(op->ptr, "export_mesh_colors");
  const bool export_normals = RNA_boolean_get(op->ptr, "export_normals");
  const bool export_materials = RNA_boolean_get(op->ptr, "export_materials");
  const eSubdivExportMode export_subdiv = eSubdivExportMode(
      RNA_enum_get(op->ptr, "export_subdivision"));

  const bool export_meshes = RNA_boolean_get(op->ptr, "export_meshes");
  const bool export_lights = RNA_boolean_get(op->ptr, "export_lights");
  const bool export_cameras = RNA_boolean_get(op->ptr, "export_cameras");
  const bool export_curves = RNA_boolean_get(op->ptr, "export_curves");
  const bool export_points = RNA_boolean_get(op->ptr, "export_points");
  const bool export_volumes = RNA_boolean_get(op->ptr, "export_volumes");

  const bool use_instancing = RNA_boolean_get(op->ptr, "use_instancing");
  const bool evaluation_mode = RNA_enum_get(op->ptr, "evaluation_mode");

  const bool generate_preview_surface = RNA_boolean_get(op->ptr, "generate_preview_surface");
  const bool generate_materialx_network = RNA_boolean_get(op->ptr, "generate_materialx_network");
  const bool overwrite_textures = RNA_boolean_get(op->ptr, "overwrite_textures");
  const bool relative_paths = RNA_boolean_get(op->ptr, "relative_paths");

  const bool export_armatures = RNA_boolean_get(op->ptr, "export_armatures");
  const bool export_shapekeys = RNA_boolean_get(op->ptr, "export_shapekeys");
  const bool only_deform_bones = RNA_boolean_get(op->ptr, "only_deform_bones");

  const bool export_custom_properties = RNA_boolean_get(op->ptr, "export_custom_properties");
  const bool author_blender_name = RNA_boolean_get(op->ptr, "author_blender_name");

  const bool triangulate_meshes = RNA_boolean_get(op->ptr, "triangulate_meshes");
  const int quad_method = RNA_enum_get(op->ptr, "quad_method");
  const int ngon_method = RNA_enum_get(op->ptr, "ngon_method");

  const bool convert_orientation = RNA_boolean_get(op->ptr, "convert_orientation");

  const int global_forward = RNA_enum_get(op->ptr, "export_global_forward_selection");
  const int global_up = RNA_enum_get(op->ptr, "export_global_up_selection");

  const bool convert_world_material = RNA_boolean_get(op->ptr, "convert_world_material");

  const eUSDXformOpMode xform_op_mode = eUSDXformOpMode(RNA_enum_get(op->ptr, "xform_op_mode"));

  const eUSDZTextureDownscaleSize usdz_downscale_size = eUSDZTextureDownscaleSize(
      RNA_enum_get(op->ptr, "usdz_downscale_size"));

  const int usdz_downscale_custom_size = RNA_int_get(op->ptr, "usdz_downscale_custom_size");

  const bool merge_parent_xform = RNA_boolean_get(op->ptr, "merge_parent_xform");

#  if PXR_VERSION >= 2403
  const bool allow_unicode = RNA_boolean_get(op->ptr, "allow_unicode");
#  else
  const bool allow_unicode = false;
#  endif

  /* When the texture export settings were moved into an enum this bit
   * became more involved, but it needs to stick around for API backwards
   * compatibility until Blender 5.0. */

  const eUSDTexExportMode textures_mode = eUSDTexExportMode(
      RNA_enum_get(op->ptr, "export_textures_mode"));
  bool export_textures = RNA_boolean_get(op->ptr, "export_textures");
  bool use_original_paths = false;

  if (!export_textures) {
    switch (textures_mode) {
      case eUSDTexExportMode::USD_TEX_EXPORT_PRESERVE:
        export_textures = false;
        use_original_paths = true;
        break;
      case eUSDTexExportMode::USD_TEX_EXPORT_NEW_PATH:
        export_textures = true;
        use_original_paths = false;
        break;
      default:
        use_original_paths = false;
    }
  }

  char root_prim_path[FILE_MAX];
  RNA_string_get(op->ptr, "root_prim_path", root_prim_path);
  process_prim_path(root_prim_path);

  char custom_properties_namespace[MAX_IDPROP_NAME];
  RNA_string_get(op->ptr, "custom_properties_namespace", custom_properties_namespace);

  USDExportParams params;
  params.export_animation = export_animation;
  params.selected_objects_only = selected_objects_only;
  params.visible_objects_only = visible_objects_only;

  params.export_meshes = export_meshes;
  params.export_lights = export_lights;
  params.export_cameras = export_cameras;
  params.export_curves = export_curves;
  params.export_points = export_points;
  params.export_volumes = export_volumes;
  params.export_hair = export_hair;
  params.export_uvmaps = export_uvmaps;
  params.rename_uvmaps = rename_uvmaps;
  params.export_normals = export_normals;
  params.export_mesh_colors = export_mesh_colors;
  params.export_materials = export_materials;

  params.export_armatures = export_armatures;
  params.export_shapekeys = export_shapekeys;
  params.only_deform_bones = only_deform_bones;

  params.convert_world_material = convert_world_material;

  params.use_instancing = use_instancing;
  params.export_custom_properties = export_custom_properties;
  params.author_blender_name = author_blender_name;
  params.allow_unicode = allow_unicode;

  params.export_subdiv = export_subdiv;
  params.evaluation_mode = eEvaluationMode(evaluation_mode);

  params.generate_preview_surface = generate_preview_surface;
  params.generate_materialx_network = generate_materialx_network;
  params.export_textures = export_textures;
  params.overwrite_textures = overwrite_textures;
  params.relative_paths = relative_paths;
  params.use_original_paths = use_original_paths;

  params.triangulate_meshes = triangulate_meshes;
  params.quad_method = quad_method;
  params.ngon_method = ngon_method;

  params.convert_orientation = convert_orientation;
  params.forward_axis = eIOAxis(global_forward);
  params.up_axis = eIOAxis(global_up);
  params.xform_op_mode = xform_op_mode;

  params.usdz_downscale_size = usdz_downscale_size;
  params.usdz_downscale_custom_size = usdz_downscale_custom_size;

  params.merge_parent_xform = merge_parent_xform;

  STRNCPY(params.root_prim_path, root_prim_path);
  STRNCPY(params.custom_properties_namespace, custom_properties_namespace);
  RNA_string_get(op->ptr, "collection", params.collection);

  bool ok = USD_export(C, filepath, &params, as_background_job, op->reports);

  return as_background_job || ok ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

static void wm_usd_export_draw(bContext *C, wmOperator *op)
{
  uiLayout *layout = op->layout;
  PointerRNA *ptr = op->ptr;

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  if (uiLayout *panel = uiLayoutPanel(C, layout, "USD_export_general", false, IFACE_("General"))) {
    uiLayout *col = uiLayoutColumn(panel, false);
    uiItemR(col, ptr, "root_prim_path", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    uiLayout *sub = uiLayoutColumnWithHeading(col, true, IFACE_("Include"));
    if (CTX_wm_space_file(C)) {
      uiItemR(sub, ptr, "selected_objects_only", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      uiItemR(sub, ptr, "visible_objects_only", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    }
    uiItemR(sub, ptr, "export_animation", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    sub = uiLayoutColumnWithHeading(col, true, IFACE_("Blender Data"));
    uiItemR(sub, ptr, "export_custom_properties", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiLayout *props_col = uiLayoutColumn(sub, true);
    uiItemR(props_col, ptr, "custom_properties_namespace", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(props_col, ptr, "author_blender_name", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiLayoutSetActive(props_col, RNA_boolean_get(op->ptr, "export_custom_properties"));
#  if PXR_VERSION >= 2403
    uiItemR(sub, ptr, "allow_unicode", UI_ITEM_NONE, std::nullopt, ICON_NONE);
#  endif

    sub = uiLayoutColumnWithHeading(col, true, IFACE_("File References"));
    uiItemR(sub, ptr, "relative_paths", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    col = uiLayoutColumn(panel, false);
    uiItemR(col, ptr, "convert_orientation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    if (RNA_boolean_get(ptr, "convert_orientation")) {
      uiItemR(col, ptr, "export_global_forward_selection", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      uiItemR(col, ptr, "export_global_up_selection", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    }
    uiItemR(col, ptr, "xform_op_mode", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    col = uiLayoutColumn(panel, false);
    uiItemR(col, ptr, "evaluation_mode", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  if (uiLayout *panel = uiLayoutPanel(
          C, layout, "USD_export_types", false, IFACE_("Object Types")))
  {
    uiLayout *col = uiLayoutColumn(panel, false);
    uiItemR(col, ptr, "export_meshes", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "export_lights", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "export_cameras", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "export_curves", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "export_points", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "export_volumes", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "export_hair", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  if (uiLayout *panel = uiLayoutPanel(C, layout, "USD_export_geometry", false, IFACE_("Geometry")))
  {
    uiLayout *col = uiLayoutColumn(panel, false);
    uiItemR(col, ptr, "export_uvmaps", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "rename_uvmaps", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "export_normals", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    uiItemR(col, ptr, "merge_parent_xform", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "triangulate_meshes", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    if (RNA_boolean_get(ptr, "triangulate_meshes")) {
      uiItemR(col, ptr, "quad_method", UI_ITEM_NONE, IFACE_("Method Quads"), ICON_NONE);
      uiItemR(col, ptr, "ngon_method", UI_ITEM_NONE, IFACE_("Polygons"), ICON_NONE);
    }

    uiItemR(col, ptr, "export_subdivision", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  if (uiLayout *panel = uiLayoutPanel(C, layout, "USD_export_rigging", true, IFACE_("Rigging"))) {
    uiLayout *col = uiLayoutColumn(panel, false);

    uiItemR(col, ptr, "export_shapekeys", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "export_armatures", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    uiLayout *row = uiLayoutRow(col, true);
    uiItemR(row, ptr, "only_deform_bones", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiLayoutSetActive(row, RNA_boolean_get(ptr, "export_armatures"));
  }

  {
    PanelLayout panel = uiLayoutPanel(C, layout, "USD_export_materials", true);
    uiLayoutSetPropSep(panel.header, false);
    uiItemR(panel.header, ptr, "export_materials", UI_ITEM_NONE, "", ICON_NONE);
    uiItemL(panel.header, IFACE_("Materials"), ICON_NONE);
    if (panel.body) {
      const bool export_materials = RNA_boolean_get(ptr, "export_materials");
      uiLayoutSetActive(panel.body, export_materials);

      uiLayout *col = uiLayoutColumn(panel.body, false);
      uiItemR(col, ptr, "generate_preview_surface", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      uiItemR(col, ptr, "generate_materialx_network", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      uiItemR(col, ptr, "convert_world_material", UI_ITEM_NONE, std::nullopt, ICON_NONE);

      col = uiLayoutColumn(panel.body, true);
      uiLayoutSetPropSep(col, true);

      uiItemR(col, ptr, "export_textures_mode", UI_ITEM_NONE, std::nullopt, ICON_NONE);

      const eUSDTexExportMode textures_mode = eUSDTexExportMode(
          RNA_enum_get(op->ptr, "export_textures_mode"));

      uiLayout *col2 = uiLayoutColumn(col, true);
      uiLayoutSetPropSep(col2, true);
      uiLayoutSetEnabled(col2, textures_mode == USD_TEX_EXPORT_NEW_PATH);
      uiItemR(col2, ptr, "overwrite_textures", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      uiItemR(col2, ptr, "usdz_downscale_size", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      if (RNA_enum_get(ptr, "usdz_downscale_size") == USD_TEXTURE_SIZE_CUSTOM) {
        uiItemR(col2, ptr, "usdz_downscale_custom_size", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
    }
  }

  if (uiLayout *panel = uiLayoutPanel(
          C, layout, "USD_export_experimental", true, IFACE_("Experimental")))
  {
    uiLayout *col = uiLayoutColumn(panel, false);
    uiItemR(col, ptr, "use_instancing", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
}

static void free_operator_customdata(wmOperator *op)
{
  if (op->customdata) {
    MEM_freeN(op->customdata);
    op->customdata = nullptr;
  }
}

static void wm_usd_export_cancel(bContext * /*C*/, wmOperator *op)
{
  free_operator_customdata(op);
}

static bool wm_usd_export_check(bContext * /*C*/, wmOperator *op)
{
  char filepath[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filepath);

  if (!BLI_path_extension_check_n(filepath, ".usd", ".usda", ".usdc", ".usdz", nullptr)) {
    BLI_path_extension_ensure(filepath, FILE_MAX, ".usdc");
    RNA_string_set(op->ptr, "filepath", filepath);
    return true;
  }

  return false;
}

static void forward_axis_update(Main * /*main*/, Scene * /*scene*/, PointerRNA *ptr)
{
  int forward = RNA_enum_get(ptr, "export_global_forward_selection");
  int up = RNA_enum_get(ptr, "export_global_up_selection");
  if ((forward % 3) == (up % 3)) {
    RNA_enum_set(ptr, "export_global_up_selection", (up + 1) % 6);
  }
}

static void up_axis_update(Main * /*main*/, Scene * /*scene*/, PointerRNA *ptr)
{
  int forward = RNA_enum_get(ptr, "export_global_forward_selection");
  int up = RNA_enum_get(ptr, "export_global_up_selection");
  if ((forward % 3) == (up % 3)) {
    RNA_enum_set(ptr, "export_global_forward_selection", (forward + 1) % 6);
  }
}

void WM_OT_usd_export(wmOperatorType *ot)
{
  ot->name = "Export USD";
  ot->description = "Export current scene in a USD archive";
  ot->idname = "WM_OT_usd_export";

  ot->invoke = wm_usd_export_invoke;
  ot->exec = wm_usd_export_exec;
  ot->poll = WM_operator_winactive;
  ot->ui = wm_usd_export_draw;
  ot->cancel = wm_usd_export_cancel;
  ot->check = wm_usd_export_check;

  ot->flag = OPTYPE_REGISTER | OPTYPE_PRESET; /* No UNDO possible. */

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_USD,
                                 FILE_BLENDER,
                                 FILE_SAVE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_SHOW_PROPS,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);

  PropertyRNA *prop = RNA_def_string(ot->srna, "filter_glob", "*.usd", 0, "", "");
  RNA_def_property_flag(prop, PROP_HIDDEN);

  RNA_def_boolean(ot->srna,
                  "selected_objects_only",
                  false,
                  "Selection Only",
                  "Only export selected objects. Unselected parents of selected objects are "
                  "exported as empty transform");

  RNA_def_boolean(ot->srna,
                  "visible_objects_only",
                  true,
                  "Visible Only",
                  "Only export visible objects. Invisible parents of exported objects are "
                  "exported as empty transforms");

  prop = RNA_def_string(ot->srna, "collection", nullptr, MAX_IDPROP_NAME, "Collection", nullptr);
  RNA_def_property_flag(prop, PROP_HIDDEN);

  RNA_def_boolean(
      ot->srna,
      "export_animation",
      false,
      "Animation",
      "Export all frames in the render frame range, rather than only the current frame");
  RNA_def_boolean(
      ot->srna, "export_hair", false, "Hair", "Export hair particle systems as USD curves");
  RNA_def_boolean(
      ot->srna, "export_uvmaps", true, "UV Maps", "Include all mesh UV maps in the export");
  RNA_def_boolean(ot->srna,
                  "rename_uvmaps",
                  true,
                  "Rename UV Maps",
                  "Rename active render UV map to \"st\" to match USD conventions");
  RNA_def_boolean(ot->srna,
                  "export_mesh_colors",
                  true,
                  "Color Attributes",
                  "Include mesh color attributes in the export");
  RNA_def_boolean(ot->srna,
                  "export_normals",
                  true,
                  "Normals",
                  "Include normals of exported meshes in the export");
  RNA_def_boolean(ot->srna,
                  "export_materials",
                  true,
                  "Materials",
                  "Export viewport settings of materials as USD preview materials, and export "
                  "material assignments as geometry subsets");

  RNA_def_enum(ot->srna,
               "export_subdivision",
               rna_enum_usd_export_subdiv_mode_items,
               USD_SUBDIV_BEST_MATCH,
               "Subdivision",
               "Choose how subdivision modifiers will be mapped to the USD subdivision scheme "
               "during export");

  RNA_def_boolean(ot->srna,
                  "export_armatures",
                  true,
                  "Armatures",
                  "Export armatures and meshes with armature modifiers as USD skeletons and "
                  "skinned meshes");

  RNA_def_boolean(ot->srna,
                  "only_deform_bones",
                  false,
                  "Only Deform Bones",
                  "Only export deform bones and their parents");

  RNA_def_boolean(
      ot->srna, "export_shapekeys", true, "Shape Keys", "Export shape keys as USD blend shapes");

  RNA_def_boolean(ot->srna,
                  "use_instancing",
                  false,
                  "Instancing",
                  "Export instanced objects as references in USD rather than real objects");

  RNA_def_enum(ot->srna,
               "evaluation_mode",
               rna_enum_usd_export_evaluation_mode_items,
               DAG_EVAL_RENDER,
               "Use Settings for",
               "Determines visibility of objects, modifier settings, and other areas where there "
               "are different settings for viewport and rendering");

  RNA_def_boolean(ot->srna,
                  "generate_preview_surface",
                  true,
                  "USD Preview Surface Network",
                  "Generate an approximate USD Preview Surface shader "
                  "representation of a Principled BSDF node network");

  RNA_def_boolean(ot->srna,
                  "generate_materialx_network",
                  false,
                  "MaterialX Network",
                  "Generate a MaterialX network representation of the materials");

  RNA_def_boolean(
      ot->srna,
      "convert_orientation",
      false,
      "Convert Orientation",
      "Convert orientation axis to a different convention to match other applications");

  prop = RNA_def_enum(ot->srna,
                      "export_global_forward_selection",
                      io_transform_axis,
                      IO_AXIS_NEGATIVE_Z,
                      "Forward Axis",
                      "");
  RNA_def_property_update_runtime(prop, forward_axis_update);

  prop = RNA_def_enum(
      ot->srna, "export_global_up_selection", io_transform_axis, IO_AXIS_Y, "Up Axis", "");
  RNA_def_property_update_runtime(prop, up_axis_update);

  RNA_def_boolean(ot->srna,
                  "export_textures",
                  false,
                  "Export Textures",
                  "If exporting materials, export textures referenced by material nodes "
                  "to a 'textures' directory in the same directory as the USD file");

  RNA_def_enum(ot->srna,
               "export_textures_mode",
               rna_enum_usd_tex_export_mode_items,
               USD_TEX_EXPORT_NEW_PATH,
               "Export Textures",
               "Texture export method");

  RNA_def_boolean(ot->srna,
                  "overwrite_textures",
                  false,
                  "Overwrite Textures",
                  "Overwrite existing files when exporting textures");

  RNA_def_boolean(ot->srna,
                  "relative_paths",
                  true,
                  "Relative Paths",
                  "Use relative paths to reference external files (i.e. textures, volumes) in "
                  "USD, otherwise use absolute paths");

  RNA_def_enum(ot->srna,
               "xform_op_mode",
               rna_enum_usd_xform_op_mode_items,
               USD_XFORM_OP_TRS,
               "Xform Ops",
               "The type of transform operators to write");

  RNA_def_string(ot->srna,
                 "root_prim_path",
                 "/root",
                 FILE_MAX,
                 "Root Prim",
                 "If set, add a transform primitive with the given path to the stage "
                 "as the parent of all exported data");

  RNA_def_boolean(ot->srna,
                  "export_custom_properties",
                  true,
                  "Custom Properties",
                  "Export custom properties as USD attributes");

  RNA_def_string(ot->srna,
                 "custom_properties_namespace",
                 "userProperties",
                 MAX_IDPROP_NAME,
                 "Namespace",
                 "If set, add the given namespace as a prefix to exported custom property names. "
                 "This only applies to property names that do not already have a prefix "
                 "(e.g., it would apply to name 'bar' but not 'foo:bar') and does not apply "
                 "to blender object and data names which are always exported in the "
                 "'userProperties:blender' namespace");

  RNA_def_boolean(ot->srna,
                  "author_blender_name",
                  true,
                  "Blender Names",
                  "Author USD custom attributes containing the original Blender object and "
                  "object data names");

  RNA_def_boolean(
      ot->srna,
      "convert_world_material",
      true,
      "Convert World Material",
      "Convert the world material to a USD dome light. "
      "Currently works for simple materials, consisting of an environment texture "
      "connected to a background shader, with an optional vector multiply of the texture color");

#  if PXR_VERSION >= 2403
  RNA_def_boolean(
      ot->srna,
      "allow_unicode",
      false,
      "Allow Unicode",
      "Preserve UTF-8 encoded characters when writing USD prim and property names "
      "(requires software utilizing USD 24.03 or greater when opening the resulting files)");
#  endif

  RNA_def_boolean(ot->srna, "export_meshes", true, "Meshes", "Export all meshes");

  RNA_def_boolean(ot->srna, "export_lights", true, "Lights", "Export all lights");

  RNA_def_boolean(ot->srna, "export_cameras", true, "Cameras", "Export all cameras");

  RNA_def_boolean(ot->srna, "export_curves", true, "Curves", "Export all curves");

  RNA_def_boolean(ot->srna, "export_points", true, "Point Clouds", "Export all point clouds");

  RNA_def_boolean(ot->srna, "export_volumes", true, "Volumes", "Export all volumes");

  RNA_def_boolean(ot->srna,
                  "triangulate_meshes",
                  false,
                  "Triangulate Meshes",
                  "Triangulate meshes during export");

  RNA_def_enum(ot->srna,
               "quad_method",
               rna_enum_modifier_triangulate_quad_method_items,
               MOD_TRIANGULATE_QUAD_SHORTEDGE,
               "Quad Method",
               "Method for splitting the quads into triangles");

  RNA_def_enum(ot->srna,
               "ngon_method",
               rna_enum_modifier_triangulate_ngon_method_items,
               MOD_TRIANGULATE_NGON_BEAUTY,
               "N-gon Method",
               "Method for splitting the n-gons into triangles");

  RNA_def_enum(ot->srna,
               "usdz_downscale_size",
               prop_usdz_downscale_size,
               DAG_EVAL_VIEWPORT,
               "USDZ Texture Downsampling",
               "Choose a maximum size for all exported textures");

  RNA_def_int(ot->srna,
              "usdz_downscale_custom_size",
              128,
              64,
              16384,
              "USDZ Custom Downscale Size",
              "Custom size for downscaling exported textures",
              128,
              8192);

  RNA_def_boolean(ot->srna,
                  "merge_parent_xform",
                  false,
                  "Merge parent Xform",
                  "Merge USD primitives with their Xform parent if possible: "
                  "USD does not allow nested UsdGeomGprim. Intermediary Xform will "
                  "be defined to keep the USD file valid.");
}

/* ====== USD Import ====== */

static int wm_usd_import_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  eUSDOperatorOptions *options = MEM_cnew<eUSDOperatorOptions>("eUSDOperatorOptions");
  options->as_background_job = true;
  op->customdata = options;

  return blender::ed::io::filesel_drop_import_invoke(C, op, event);
}

static int wm_usd_import_exec(bContext *C, wmOperator *op)
{
  if (!RNA_struct_property_is_set_ex(op->ptr, "filepath", false)) {
    BKE_report(op->reports, RPT_ERROR, "No filepath given");
    return OPERATOR_CANCELLED;
  }

  char filepath[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filepath);

  eUSDOperatorOptions *options = static_cast<eUSDOperatorOptions *>(op->customdata);
  const bool as_background_job = (options != nullptr && options->as_background_job);
  MEM_SAFE_FREE(op->customdata);

  const float scale = RNA_float_get(op->ptr, "scale");

  const bool set_frame_range = RNA_boolean_get(op->ptr, "set_frame_range");

  const bool read_mesh_uvs = RNA_boolean_get(op->ptr, "read_mesh_uvs");
  const bool read_mesh_colors = RNA_boolean_get(op->ptr, "read_mesh_colors");
  const bool read_mesh_attributes = RNA_boolean_get(op->ptr, "read_mesh_attributes");

  char mesh_read_flag = MOD_MESHSEQ_READ_VERT | MOD_MESHSEQ_READ_POLY;
  if (read_mesh_uvs) {
    mesh_read_flag |= MOD_MESHSEQ_READ_UV;
  }
  if (read_mesh_colors) {
    mesh_read_flag |= MOD_MESHSEQ_READ_COLOR;
  }
  if (read_mesh_attributes) {
    mesh_read_flag |= MOD_MESHSEQ_READ_ATTRIBUTES;
  }

  const bool import_cameras = RNA_boolean_get(op->ptr, "import_cameras");
  const bool import_curves = RNA_boolean_get(op->ptr, "import_curves");
  const bool import_lights = RNA_boolean_get(op->ptr, "import_lights");
  const bool import_materials = RNA_boolean_get(op->ptr, "import_materials");
  const bool import_meshes = RNA_boolean_get(op->ptr, "import_meshes");
  const bool import_volumes = RNA_boolean_get(op->ptr, "import_volumes");
  const bool import_shapes = RNA_boolean_get(op->ptr, "import_shapes");
  const bool import_skeletons = RNA_boolean_get(op->ptr, "import_skeletons");
  const bool import_blendshapes = RNA_boolean_get(op->ptr, "import_blendshapes");
  const bool import_points = RNA_boolean_get(op->ptr, "import_points");

  const bool import_subdiv = RNA_boolean_get(op->ptr, "import_subdiv");

  const bool support_scene_instancing = RNA_boolean_get(op->ptr, "support_scene_instancing");

  const bool import_visible_only = RNA_boolean_get(op->ptr, "import_visible_only");

  const bool import_defined_only = RNA_boolean_get(op->ptr, "import_defined_only");

  const bool create_collection = RNA_boolean_get(op->ptr, "create_collection");

  char *prim_path_mask = RNA_string_get_alloc(op->ptr, "prim_path_mask", nullptr, 0, nullptr);

  const bool import_guide = RNA_boolean_get(op->ptr, "import_guide");
  const bool import_proxy = RNA_boolean_get(op->ptr, "import_proxy");
  const bool import_render = RNA_boolean_get(op->ptr, "import_render");

  const bool import_all_materials = RNA_boolean_get(op->ptr, "import_all_materials");

  const bool import_usd_preview = RNA_boolean_get(op->ptr, "import_usd_preview");
  const bool set_material_blend = RNA_boolean_get(op->ptr, "set_material_blend");

  const float light_intensity_scale = RNA_float_get(op->ptr, "light_intensity_scale");

  const eUSDMtlPurpose mtl_purpose = eUSDMtlPurpose(RNA_enum_get(op->ptr, "mtl_purpose"));
  const eUSDMtlNameCollisionMode mtl_name_collision_mode = eUSDMtlNameCollisionMode(
      RNA_enum_get(op->ptr, "mtl_name_collision_mode"));

  const eUSDAttrImportMode attr_import_mode = eUSDAttrImportMode(
      RNA_enum_get(op->ptr, "attr_import_mode"));

  const bool validate_meshes = RNA_boolean_get(op->ptr, "validate_meshes");

  const bool create_world_material = RNA_boolean_get(op->ptr, "create_world_material");

  const bool merge_parent_xform = RNA_boolean_get(op->ptr, "merge_parent_xform");

  /* TODO(makowalski): Add support for sequences. */
  const bool is_sequence = false;
  int offset = 0;
  int sequence_len = 1;

  const eUSDTexImportMode import_textures_mode = eUSDTexImportMode(
      RNA_enum_get(op->ptr, "import_textures_mode"));

  char import_textures_dir[FILE_MAXDIR];
  RNA_string_get(op->ptr, "import_textures_dir", import_textures_dir);

  const eUSDTexNameCollisionMode tex_name_collision_mode = eUSDTexNameCollisionMode(
      RNA_enum_get(op->ptr, "tex_name_collision_mode"));

  USDImportParams params{};
  params.prim_path_mask = prim_path_mask;
  params.scale = scale;
  params.light_intensity_scale = light_intensity_scale;

  params.mesh_read_flag = mesh_read_flag;
  params.set_frame_range = set_frame_range;
  params.is_sequence = is_sequence;
  params.sequence_len = sequence_len;
  params.offset = offset;

  params.import_visible_only = import_visible_only;
  params.import_defined_only = import_defined_only;

  params.import_cameras = import_cameras;
  params.import_curves = import_curves;
  params.import_lights = import_lights;
  params.import_materials = import_materials;
  params.import_all_materials = import_all_materials;
  params.import_meshes = import_meshes;
  params.import_points = import_points;
  params.import_subdiv = import_subdiv;
  params.import_volumes = import_volumes;

  params.create_collection = create_collection;
  params.create_world_material = create_world_material;
  params.support_scene_instancing = support_scene_instancing;

  params.import_shapes = import_shapes;
  params.import_skeletons = import_skeletons;
  params.import_blendshapes = import_blendshapes;

  params.validate_meshes = validate_meshes;
  params.merge_parent_xform = merge_parent_xform;

  params.import_guide = import_guide;
  params.import_proxy = import_proxy;
  params.import_render = import_render;

  params.import_usd_preview = import_usd_preview;
  params.set_material_blend = set_material_blend;
  params.mtl_purpose = mtl_purpose;
  params.mtl_name_collision_mode = mtl_name_collision_mode;
  params.import_textures_mode = import_textures_mode;
  params.tex_name_collision_mode = tex_name_collision_mode;

  params.attr_import_mode = attr_import_mode;

  STRNCPY(params.import_textures_dir, import_textures_dir);

  /* Switch out of edit mode to avoid being stuck in it (#54326). */
  Object *obedit = CTX_data_edit_object(C);
  if (obedit) {
    blender::ed::object::mode_set(C, OB_MODE_EDIT);
  }

  const bool ok = USD_import(C, filepath, &params, as_background_job, op->reports);

  return as_background_job || ok ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

static void wm_usd_import_cancel(bContext * /*C*/, wmOperator *op)
{
  free_operator_customdata(op);
}

static void wm_usd_import_draw(bContext *C, wmOperator *op)
{
  uiLayout *layout = op->layout;
  PointerRNA *ptr = op->ptr;

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  if (uiLayout *panel = uiLayoutPanel(C, layout, "USD_import_general", false, IFACE_("General"))) {
    uiLayout *col = uiLayoutColumn(panel, false);

    uiItemR(col, ptr, "prim_path_mask", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    uiLayout *sub = uiLayoutColumnWithHeading(col, true, IFACE_("Include"));
    uiItemR(sub, ptr, "import_visible_only", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(sub, ptr, "import_defined_only", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    col = uiLayoutColumn(panel, false);
    uiItemR(col, ptr, "set_frame_range", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "create_collection", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "relative_path", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    uiItemR(col, ptr, "scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "light_intensity_scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "attr_import_mode", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  if (uiLayout *panel = uiLayoutPanel(
          C, layout, "USD_import_types", false, IFACE_("Object Types")))
  {
    uiLayout *col = uiLayoutColumn(panel, false);
    uiItemR(col, ptr, "import_cameras", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "import_curves", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "import_lights", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "import_materials", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "import_meshes", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "import_volumes", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "import_points", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "import_shapes", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    col = uiLayoutColumnWithHeading(panel, true, IFACE_("Display Purpose"));
    uiItemR(col, ptr, "import_render", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "import_proxy", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "import_guide", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    col = uiLayoutColumnWithHeading(panel, true, IFACE_("Material Purpose"));
    uiItemR(col, ptr, "mtl_purpose", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  if (uiLayout *panel = uiLayoutPanel(C, layout, "USD_import_geometry", true, IFACE_("Geometry")))
  {
    uiLayout *col = uiLayoutColumn(panel, false);
    uiItemR(col, ptr, "read_mesh_uvs", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "read_mesh_colors", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "read_mesh_attributes", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "import_subdiv", UI_ITEM_NONE, IFACE_("Subdivision"), ICON_NONE);

    col = uiLayoutColumn(panel, false);
    uiItemR(col, ptr, "validate_meshes", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "merge_parent_xform", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  if (uiLayout *panel = uiLayoutPanel(C, layout, "USD_import_rigging", true, IFACE_("Rigging"))) {
    uiLayout *col = uiLayoutColumn(panel, false);
    uiItemR(col, ptr, "import_blendshapes", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "import_skeletons", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  if (uiLayout *panel = uiLayoutPanel(C, layout, "USD_import_material", true, IFACE_("Materials")))
  {
    uiLayout *col = uiLayoutColumn(panel, false);

    uiItemR(col, ptr, "import_all_materials", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "import_usd_preview", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiItemR(col, ptr, "create_world_material", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiLayoutSetEnabled(col, RNA_boolean_get(ptr, "import_materials"));

    uiLayout *row = uiLayoutRow(col, true);
    uiItemR(row, ptr, "set_material_blend", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiLayoutSetEnabled(row, RNA_boolean_get(ptr, "import_usd_preview"));
    uiItemR(col, ptr, "mtl_name_collision_mode", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  if (uiLayout *panel = uiLayoutPanel(C, layout, "USD_import_texture", true, IFACE_("Textures"))) {
    uiLayout *col = uiLayoutColumn(panel, false);

    uiItemR(col, ptr, "import_textures_mode", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    bool copy_textures = RNA_enum_get(op->ptr, "import_textures_mode") == USD_TEX_IMPORT_COPY;

    uiLayout *row = uiLayoutRow(col, true);
    uiItemR(row, ptr, "import_textures_dir", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiLayoutSetEnabled(row, copy_textures);
    row = uiLayoutRow(col, true);
    uiItemR(row, ptr, "tex_name_collision_mode", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    uiLayoutSetEnabled(row, copy_textures);
    uiLayoutSetEnabled(col, RNA_boolean_get(ptr, "import_materials"));
  }

  if (uiLayout *panel = uiLayoutPanel(
          C, layout, "USD_import_instancing", true, IFACE_("Particles and Instancing")))
  {
    uiLayout *col = uiLayoutColumn(panel, false);
    uiItemR(col, ptr, "support_scene_instancing", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
}

void WM_OT_usd_import(wmOperatorType *ot)
{
  ot->name = "Import USD";
  ot->description = "Import USD stage into current scene";
  ot->idname = "WM_OT_usd_import";

  ot->invoke = wm_usd_import_invoke;
  ot->exec = wm_usd_import_exec;
  ot->cancel = wm_usd_import_cancel;
  ot->poll = WM_operator_winactive;
  ot->ui = wm_usd_import_draw;

  ot->flag = OPTYPE_UNDO | OPTYPE_PRESET;

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_USD,
                                 FILE_BLENDER,
                                 FILE_OPENFILE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_RELPATH | WM_FILESEL_SHOW_PROPS,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);

  PropertyRNA *prop = RNA_def_string(ot->srna, "filter_glob", "*.usd", 0, "", "");
  RNA_def_property_flag(prop, PROP_HIDDEN);

  RNA_def_float(
      ot->srna,
      "scale",
      1.0f,
      0.0001f,
      1000.0f,
      "Scale",
      "Value by which to enlarge or shrink the objects with respect to the world's origin",
      0.0001f,
      1000.0f);

  RNA_def_boolean(ot->srna,
                  "set_frame_range",
                  true,
                  "Set Frame Range",
                  "Update the scene's start and end frame to match those of the USD archive");

  RNA_def_boolean(ot->srna, "import_cameras", true, "Cameras", "");
  RNA_def_boolean(ot->srna, "import_curves", true, "Curves", "");
  RNA_def_boolean(ot->srna, "import_lights", true, "Lights", "");
  RNA_def_boolean(ot->srna, "import_materials", true, "Materials", "");
  RNA_def_boolean(ot->srna, "import_meshes", true, "Meshes", "");
  RNA_def_boolean(ot->srna, "import_volumes", true, "Volumes", "");
  RNA_def_boolean(ot->srna, "import_shapes", true, "USD Shapes", "");
  RNA_def_boolean(ot->srna, "import_skeletons", true, "Armatures", "");
  RNA_def_boolean(ot->srna, "import_blendshapes", true, "Shape Keys", "");
  RNA_def_boolean(ot->srna, "import_points", true, "Point Clouds", "");

  RNA_def_boolean(ot->srna,
                  "import_subdiv",
                  false,
                  "Import Subdivision Scheme",
                  "Create subdivision surface modifiers based on the USD "
                  "SubdivisionScheme attribute");

  RNA_def_boolean(ot->srna,
                  "support_scene_instancing",
                  true,
                  "Scene Instancing",
                  "Import USD scene graph instances as collection instances");

  RNA_def_boolean(ot->srna,
                  "import_visible_only",
                  true,
                  "Visible Primitives Only",
                  "Do not import invisible USD primitives. "
                  "Only applies to primitives with a non-animated visibility attribute. "
                  "Primitives with animated visibility will always be imported");

  RNA_def_boolean(ot->srna,
                  "create_collection",
                  false,
                  "Create Collection",
                  "Add all imported objects to a new collection");

  RNA_def_boolean(ot->srna, "read_mesh_uvs", true, "UV Coordinates", "Read mesh UV coordinates");

  RNA_def_boolean(
      ot->srna, "read_mesh_colors", true, "Color Attributes", "Read mesh color attributes");

  RNA_def_boolean(ot->srna,
                  "read_mesh_attributes",
                  true,
                  "Mesh Attributes",
                  "Read USD Primvars as mesh attributes");

  RNA_def_string(ot->srna,
                 "prim_path_mask",
                 nullptr,
                 0,
                 "Path Mask",
                 "Import only the primitive at the given path and its descendants. "
                 "Multiple paths may be specified in a list delimited by commas or semicolons");

  RNA_def_boolean(ot->srna, "import_guide", false, "Guide", "Import guide geometry");

  RNA_def_boolean(ot->srna, "import_proxy", false, "Proxy", "Import proxy geometry");

  RNA_def_boolean(ot->srna, "import_render", true, "Render", "Import final render geometry");

  RNA_def_boolean(ot->srna,
                  "import_all_materials",
                  false,
                  "Import All Materials",
                  "Also import materials that are not used by any geometry. "
                  "Note that when this option is false, materials referenced "
                  "by geometry will still be imported");

  RNA_def_boolean(ot->srna,
                  "import_usd_preview",
                  true,
                  "Import USD Preview",
                  "Convert UsdPreviewSurface shaders to Principled BSDF shader networks");

  RNA_def_boolean(ot->srna,
                  "set_material_blend",
                  true,
                  "Set Material Blend",
                  "If the Import USD Preview option is enabled, "
                  "the material blend method will automatically be set based on the "
                  "shader's opacity and opacityThreshold inputs");

  RNA_def_float(ot->srna,
                "light_intensity_scale",
                1.0f,
                0.0001f,
                10000.0f,
                "Light Intensity Scale",
                "Scale for the intensity of imported lights",
                0.0001f,
                1000.0f);

  RNA_def_enum(ot->srna,
               "mtl_purpose",
               rna_enum_usd_mtl_purpose_items,
               USD_MTL_PURPOSE_FULL,
               "Material Purpose",
               "Attempt to import materials with the given purpose. "
               "If no material with this purpose is bound to the primitive, "
               "fall back on loading any other bound material");

  RNA_def_enum(
      ot->srna,
      "mtl_name_collision_mode",
      rna_enum_usd_mtl_name_collision_mode_items,
      USD_MTL_NAME_COLLISION_MAKE_UNIQUE,
      "Material Name Collision",
      "Behavior when the name of an imported material conflicts with an existing material");

  RNA_def_enum(ot->srna,
               "import_textures_mode",
               rna_enum_usd_tex_import_mode_items,
               USD_TEX_IMPORT_PACK,
               "Import Textures",
               "Behavior when importing textures from a USDZ archive");

  RNA_def_string(ot->srna,
                 "import_textures_dir",
                 "//textures/",
                 FILE_MAXDIR,
                 "Textures Directory",
                 "Path to the directory where imported textures will be copied");

  RNA_def_enum(
      ot->srna,
      "tex_name_collision_mode",
      rna_enum_usd_tex_name_collision_mode_items,
      USD_TEX_NAME_COLLISION_USE_EXISTING,
      "File Name Collision",
      "Behavior when the name of an imported texture file conflicts with an existing file");

  RNA_def_enum(ot->srna,
               "attr_import_mode",
               rna_enum_usd_attr_import_mode_items,
               USD_ATTR_IMPORT_ALL,
               "Custom Properties",
               "Behavior when importing USD attributes as Blender custom properties");

  RNA_def_boolean(
      ot->srna,
      "validate_meshes",
      false,
      "Validate Meshes",
      "Ensure the data is valid "
      "(when disabled, data may be imported which causes crashes displaying or editing)");

  RNA_def_boolean(ot->srna,
                  "create_world_material",
                  true,
                  "Create World Material",
                  "Convert the first discovered USD dome light to a world background shader");

  RNA_def_boolean(ot->srna,
                  "import_defined_only",
                  true,
                  "Defined Primitives Only",
                  "Import only defined USD primitives. When disabled this allows importing USD "
                  "primitives which are not defined, such as those with an override specifier");

  RNA_def_boolean(ot->srna,
                  "merge_parent_xform",
                  true,
                  "Merge parent Xform",
                  "Allow USD primitives to merge with their Xform parent "
                  "if they are the only child in the hierarchy");
}

namespace blender::ed::io {
void usd_file_handler_add()
{
  auto fh = std::make_unique<blender::bke::FileHandlerType>();
  STRNCPY(fh->idname, "IO_FH_usd");
  STRNCPY(fh->import_operator, "WM_OT_usd_import");
  STRNCPY(fh->export_operator, "WM_OT_usd_export");
  STRNCPY(fh->label, "Universal Scene Description");
  STRNCPY(fh->file_extensions_str, ".usd;.usda;.usdc;.usdz");
  fh->poll_drop = poll_file_object_drop;
  bke::file_handler_add(std::move(fh));
}
}  // namespace blender::ed::io

#endif /* WITH_USD */
