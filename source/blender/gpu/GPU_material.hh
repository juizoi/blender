/* SPDX-FileCopyrightText: 2005 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup gpu
 */

#pragma once

#include <string>

#include "DNA_customdata_types.h" /* for eCustomDataType */
#include "DNA_image_types.h"
#include "DNA_listBase.h"

#include "GPU_shader.hh"  /* for GPUShaderCreateInfo */
#include "GPU_texture.hh" /* for GPUSamplerState */

struct GHash;
struct GPUMaterial;
struct GPUNodeLink;
struct GPUNodeStack;
struct GPUPass;
struct GPUTexture;
struct GPUUniformBuf;
struct Image;
struct ImageUser;
struct ListBase;
struct Main;
struct Material;
struct Scene;
struct bNode;
struct bNodeTree;

/* Functions to create GPU Materials nodes. */

enum eGPUType {
  /* Keep in sync with GPU_DATATYPE_STR */
  /* The value indicates the number of elements in each type */
  GPU_NONE = 0,
  GPU_FLOAT = 1,
  GPU_VEC2 = 2,
  GPU_VEC3 = 3,
  GPU_VEC4 = 4,
  GPU_MAT3 = 9,
  GPU_MAT4 = 16,
  GPU_MAX_CONSTANT_DATA = GPU_MAT4,

  /* Values not in GPU_DATATYPE_STR */
  GPU_TEX1D_ARRAY = 1001,
  GPU_TEX2D = 1002,
  GPU_TEX2D_ARRAY = 1003,
  GPU_TEX3D = 1004,

  /* GLSL Struct types */
  GPU_CLOSURE = 1007,

  /* Opengl Attributes */
  GPU_ATTR = 3001,
};

enum eGPUMaterialFlag {
  GPU_MATFLAG_DIFFUSE = (1 << 0),
  GPU_MATFLAG_SUBSURFACE = (1 << 1),
  GPU_MATFLAG_GLOSSY = (1 << 2),
  GPU_MATFLAG_REFRACT = (1 << 3),
  GPU_MATFLAG_EMISSION = (1 << 4),
  GPU_MATFLAG_TRANSPARENT = (1 << 5),
  GPU_MATFLAG_HOLDOUT = (1 << 6),
  GPU_MATFLAG_SHADER_TO_RGBA = (1 << 7),
  GPU_MATFLAG_AO = (1 << 8),
  /* Signals the presence of multiple reflection closures. */
  GPU_MATFLAG_COAT = (1 << 9),
  GPU_MATFLAG_TRANSLUCENT = (1 << 10),

  GPU_MATFLAG_VOLUME_SCATTER = (1 << 16),
  GPU_MATFLAG_VOLUME_ABSORPTION = (1 << 17),

  GPU_MATFLAG_OBJECT_INFO = (1 << 18),
  GPU_MATFLAG_AOV = (1 << 19),

  GPU_MATFLAG_BARYCENTRIC = (1 << 20),

  /* Tells the render engine the material was just compiled or updated. */
  GPU_MATFLAG_UPDATED = (1 << 29),

  /* HACK(fclem) Tells the environment texture node to not bail out if empty. */
  GPU_MATFLAG_LOOKDEV_HACK = (1 << 30),
};

ENUM_OPERATORS(eGPUMaterialFlag, GPU_MATFLAG_LOOKDEV_HACK);

struct GPUNodeStack {
  eGPUType type;
  float vec[4];
  GPUNodeLink *link;
  bool hasinput;
  bool hasoutput;
  short sockettype;
  bool end;
};

enum eGPUMaterialStatus {
  GPU_MAT_FAILED = 0,
  GPU_MAT_CREATED,
  GPU_MAT_QUEUED,
  GPU_MAT_SUCCESS,
};

/* GPU_MAT_OPTIMIZATION_SKIP for cases where we do not
 * plan to perform optimization on a given material. */
enum eGPUMaterialOptimizationStatus {
  GPU_MAT_OPTIMIZATION_SKIP = 0,
  GPU_MAT_OPTIMIZATION_READY,
  GPU_MAT_OPTIMIZATION_QUEUED,
  GPU_MAT_OPTIMIZATION_SUCCESS,
};

enum eGPUDefaultValue {
  GPU_DEFAULT_0 = 0,
  GPU_DEFAULT_1,
};

struct GPUCodegenOutput {
  std::string attr_load;
  /* Node-tree functions calls. */
  std::string displacement;
  std::string surface;
  std::string volume;
  std::string thickness;
  std::string composite;
  std::string material_functions;

  GPUShaderCreateInfo *create_info;
};

using GPUCodegenCallbackFn = void (*)(void *thunk, GPUMaterial *mat, GPUCodegenOutput *codegen);
/**
 * Should return an already compiled pass if it's functionally equivalent to the one being
 * compiled.
 */
using GPUMaterialPassReplacementCallbackFn = GPUPass *(*)(void *thunk, GPUMaterial *mat);

GPUNodeLink *GPU_constant(const float *num);
GPUNodeLink *GPU_uniform(const float *num);
GPUNodeLink *GPU_attribute(GPUMaterial *mat, eCustomDataType type, const char *name);
/**
 * Add a GPU attribute that refers to the default color attribute on a geometry.
 * The name, type, and domain are unknown and do not depend on the material.
 */
GPUNodeLink *GPU_attribute_default_color(GPUMaterial *mat);
/**
 * Add a GPU attribute that refers to the approximate length of curves/hairs.
 */
GPUNodeLink *GPU_attribute_hair_length(GPUMaterial *mat);
GPUNodeLink *GPU_attribute_with_default(GPUMaterial *mat,
                                        eCustomDataType type,
                                        const char *name,
                                        eGPUDefaultValue default_value);
GPUNodeLink *GPU_uniform_attribute(GPUMaterial *mat,
                                   const char *name,
                                   bool use_dupli,
                                   uint32_t *r_hash);
GPUNodeLink *GPU_layer_attribute(GPUMaterial *mat, const char *name);
GPUNodeLink *GPU_image(GPUMaterial *mat,
                       Image *ima,
                       ImageUser *iuser,
                       GPUSamplerState sampler_state);
void GPU_image_tiled(GPUMaterial *mat,
                     Image *ima,
                     ImageUser *iuser,
                     GPUSamplerState sampler_state,
                     GPUNodeLink **r_image_tiled_link,
                     GPUNodeLink **r_image_tiled_mapping_link);
GPUNodeLink *GPU_image_sky(GPUMaterial *mat,
                           int width,
                           int height,
                           const float *pixels,
                           float *layer,
                           GPUSamplerState sampler_state);
GPUNodeLink *GPU_color_band(GPUMaterial *mat, int size, float *pixels, float *r_row);

/**
 * Create an implementation defined differential calculation of a float function.
 * The given function should return a float.
 * The result will be a vec2 containing dFdx and dFdy result of that function.
 */
GPUNodeLink *GPU_differentiate_float_function(const char *function_name, const float filter_width);

bool GPU_link(GPUMaterial *mat, const char *name, ...);
bool GPU_stack_link(GPUMaterial *mat,
                    const bNode *node,
                    const char *name,
                    GPUNodeStack *in,
                    GPUNodeStack *out,
                    ...);

void GPU_material_output_surface(GPUMaterial *material, GPUNodeLink *link);
void GPU_material_output_volume(GPUMaterial *material, GPUNodeLink *link);
void GPU_material_output_displacement(GPUMaterial *material, GPUNodeLink *link);
void GPU_material_output_thickness(GPUMaterial *material, GPUNodeLink *link);

void GPU_material_add_output_link_aov(GPUMaterial *material, GPUNodeLink *link, int hash);

void GPU_material_add_output_link_composite(GPUMaterial *material, GPUNodeLink *link);

/**
 * Wrap a part of the material graph into a function. You need then need to call the function by
 * using something like #GPU_differentiate_float_function.
 * \note This replace the link by a constant to break the link with the main graph.
 * \param return_type: sub function return type. Output is cast to this type.
 * \param link: link to use as the sub function output.
 * \return the name of the generated function.
 */
char *GPU_material_split_sub_function(GPUMaterial *material,
                                      eGPUType return_type,
                                      GPUNodeLink **link);

/**
 * High level functions to create and use GPU materials.
 */

enum eGPUMaterialEngine {
  GPU_MAT_EEVEE_LEGACY = 0,
  GPU_MAT_EEVEE,
  GPU_MAT_COMPOSITOR,
};

GPUMaterial *GPU_material_from_nodetree(
    Scene *scene,
    Material *ma,
    bNodeTree *ntree,
    ListBase *gpumaterials,
    const char *name,
    eGPUMaterialEngine engine,
    uint64_t shader_uuid,
    bool is_volume_shader,
    bool is_lookdev,
    GPUCodegenCallbackFn callback,
    void *thunk,
    GPUMaterialPassReplacementCallbackFn pass_replacement_cb = nullptr);

void GPU_material_compile(GPUMaterial *mat);
void GPU_material_free_single(GPUMaterial *material);
void GPU_material_free(ListBase *gpumaterial);

void GPU_material_async_compile(GPUMaterial *mat);
/** Returns true if the material have finished its compilation. */
bool GPU_material_async_try_finalize(GPUMaterial *mat);

void GPU_material_acquire(GPUMaterial *mat);
void GPU_material_release(GPUMaterial *mat);

void GPU_materials_free(Main *bmain);

Scene *GPU_material_scene(GPUMaterial *material);
GPUPass *GPU_material_get_pass(GPUMaterial *material);
/** Return the most optimal shader configuration for the given material. */
GPUShader *GPU_material_get_shader(GPUMaterial *material);
/** Return the base un-optimized shader. */
GPUShader *GPU_material_get_shader_base(GPUMaterial *material);
const char *GPU_material_get_name(GPUMaterial *material);

/**
 * Material Optimization.
 * \note Compiles optimal version of shader graph, populating mat->optimized_pass.
 * This operation should always be deferred until existing compilations have completed.
 * Default un-optimized materials will still exist for interactive material editing performance.
 */
void GPU_material_optimize(GPUMaterial *mat);

/**
 * Return can be null if it's a world material.
 */
Material *GPU_material_get_material(GPUMaterial *material);
/**
 * Return true if the material compilation has not yet begin or begin.
 */
eGPUMaterialStatus GPU_material_status(GPUMaterial *mat);
void GPU_material_status_set(GPUMaterial *mat, eGPUMaterialStatus status);

/**
 * Return status for asynchronous optimization jobs.
 */
eGPUMaterialOptimizationStatus GPU_material_optimization_status(GPUMaterial *mat);
void GPU_material_optimization_status_set(GPUMaterial *mat, eGPUMaterialOptimizationStatus status);
bool GPU_material_optimization_ready(GPUMaterial *mat);

/**
 * Store reference to a similar default material for asynchronous PSO cache warming.
 *
 * This function expects `material` to have not yet been compiled and for `default_material` to be
 * ready. When compiling `material` as part of an asynchronous shader compilation job, use existing
 * PSO descriptors from `default_material`'s shader to also compile PSOs for this new material
 * asynchronously, rather than at runtime.
 *
 * The default_material `options` should match this new materials options in order
 * for PSO descriptors to match those needed by the new `material`.
 *
 * NOTE: `default_material` must exist when `GPU_material_compile(..)` is called for
 * `material`.
 *
 * See `GPU_shader_warm_cache(..)` for more information.
 */
void GPU_material_set_default(GPUMaterial *material, GPUMaterial *default_material);

GPUUniformBuf *GPU_material_uniform_buffer_get(GPUMaterial *material);
/**
 * Create dynamic UBO from parameters
 *
 * \param inputs: Items are #LinkData, data is #GPUInput (`BLI_genericNodeN(GPUInput)`).
 */
void GPU_material_uniform_buffer_create(GPUMaterial *material, ListBase *inputs);

bool GPU_material_has_surface_output(GPUMaterial *mat);
bool GPU_material_has_volume_output(GPUMaterial *mat);
bool GPU_material_has_displacement_output(GPUMaterial *mat);

void GPU_material_flag_set(GPUMaterial *mat, eGPUMaterialFlag flag);
bool GPU_material_flag_get(const GPUMaterial *mat, eGPUMaterialFlag flag);
eGPUMaterialFlag GPU_material_flag(const GPUMaterial *mat);
bool GPU_material_recalc_flag_get(GPUMaterial *mat);
uint64_t GPU_material_uuid_get(GPUMaterial *mat);

void GPU_pass_cache_init();
void GPU_pass_cache_garbage_collect();
void GPU_pass_cache_free();

/* Requested Material Attributes and Textures */

struct GPUMaterialAttribute {
  GPUMaterialAttribute *next, *prev;
  int type;                /* eCustomDataType */
  char name[68];           /* MAX_CUSTOMDATA_LAYER_NAME */
  char input_name[12 + 1]; /* GPU_MAX_SAFE_ATTR_NAME + 1 */
  eGPUType gputype;
  eGPUDefaultValue default_value; /* Only for volumes attributes. */
  int id;
  int users;
  /**
   * If true, the corresponding attribute is the specified default color attribute on the mesh,
   * if it exists. In that case the type and name data can vary per geometry, so it will not be
   * valid here.
   */
  bool is_default_color;
  /**
   * If true, the attribute is the length of hair particles and curves.
   */
  bool is_hair_length;
};

struct GPUMaterialTexture {
  GPUMaterialTexture *next, *prev;
  Image *ima;
  ImageUser iuser;
  bool iuser_available;
  GPUTexture **colorband;
  GPUTexture **sky;
  char sampler_name[32];       /* Name of sampler in GLSL. */
  char tiled_mapping_name[32]; /* Name of tile mapping sampler in GLSL. */
  int users;
  GPUSamplerState sampler_state;
};

ListBase GPU_material_attributes(const GPUMaterial *material);
ListBase GPU_material_textures(GPUMaterial *material);

struct GPUUniformAttr {
  GPUUniformAttr *next, *prev;

  /* Meaningful part of the attribute set key. */
  char name[68]; /* MAX_CUSTOMDATA_LAYER_NAME */
  /** Hash of name[68] + use_dupli. */
  uint32_t hash_code;
  bool use_dupli;

  /* Helper fields used by code generation. */
  short id;
  int users;
};

struct GPUUniformAttrList {
  ListBase list; /* GPUUniformAttr */

  /* List length and hash code precomputed for fast lookup and comparison. */
  unsigned int count, hash_code;
};

const GPUUniformAttrList *GPU_material_uniform_attributes(const GPUMaterial *material);

GHash *GPU_uniform_attr_list_hash_new(const char *info);
void GPU_uniform_attr_list_copy(GPUUniformAttrList *dest, const GPUUniformAttrList *src);
void GPU_uniform_attr_list_free(GPUUniformAttrList *set);

struct GPULayerAttr {
  GPULayerAttr *next, *prev;

  /* Meaningful part of the attribute set key. */
  char name[256]; /* Multiple MAX_CUSTOMDATA_LAYER_NAME */
  /** Hash of name[68]. */
  uint32_t hash_code;

  /* Helper fields used by code generation. */
  int users;
};

const ListBase *GPU_material_layer_attributes(const GPUMaterial *material);

/* A callback passed to GPU_material_from_callbacks to construct the material graph by adding and
 * linking the necessary GPU material nodes. */
using ConstructGPUMaterialFn = void (*)(void *thunk, GPUMaterial *material);

/* Construct a GPU material from a set of callbacks. See the callback types for more information.
 * The given thunk will be passed as the first parameter of each callback. */
GPUMaterial *GPU_material_from_callbacks(eGPUMaterialEngine engine,
                                         ConstructGPUMaterialFn construct_function_cb,
                                         GPUCodegenCallbackFn generate_code_function_cb,
                                         void *thunk);
