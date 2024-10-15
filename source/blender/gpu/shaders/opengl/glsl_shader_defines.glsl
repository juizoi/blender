/* SPDX-FileCopyrightText: 2022-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** Type aliases. */
/** IMPORTANT: Be wary of size and alignment matching for types that are present
 * in C++ shared code. */

/* Matrix reshaping functions. Needs to be declared before matrix type aliases. */
#define RESHAPE(name, mat_to, mat_from) \
  mat_to to_##name(mat_from m) \
  { \
    return mat_to(m); \
  }

/* clang-format off */
RESHAPE(float2x2, mat2x2, mat3x3)
RESHAPE(float2x2, mat2x2, mat4x4)
RESHAPE(float3x3, mat3x3, mat4x4)
RESHAPE(float3x3, mat3x3, mat2x2)
RESHAPE(float4x4, mat4x4, mat2x2)
RESHAPE(float4x4, mat4x4, mat3x3)
/* clang-format on */
/* TODO(fclem): Remove. Use Transform instead. */
RESHAPE(float3x3, mat3x3, mat3x4)

#undef RESHAPE

/* Boolean in GLSL are 32bit in interface structs. */
#define bool32_t bool
#define bool2 bvec2
#define bool3 bvec3
#define bool4 bvec4

#define float2 vec2
#define float3 vec3
#define float4 vec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define uint2 uvec2
#define uint3 uvec3
#define uint4 uvec4
/* GLSL already follows the packed alignment / size rules for vec3. */
#define packed_float3 float3
#define packed_int3 int3
#define packed_uint3 uint3

#define float2x2 mat2x2
#define float3x2 mat3x2
#define float4x2 mat4x2
#define float2x3 mat2x3
#define float3x3 mat3x3
#define float4x3 mat4x3
#define float2x4 mat2x4
#define float3x4 mat3x4
#define float4x4 mat4x4

/* Small types are unavailable in GLSL (or badly supported), promote them to bigger type. */
#define char int
#define char2 int2
#define char3 int3
#define char4 int4
#define short int
#define short2 int2
#define short3 int3
#define short4 int4
#define uchar uint
#define uchar2 uint2
#define uchar3 uint3
#define uchar4 uint4
#define ushort uint
#define ushort2 uint2
#define ushort3 uint3
#define ushort4 uint4
#define half float
#define half2 float2
#define half3 float3
#define half4 float4

/* Aliases for supported fixed width types. */
#define int32_t int
#define uint32_t uint

/* Fast load/store variant macro. In GLSL this is the same as imageLoad/imageStore, but assumes no
 * bounds checking. */
#define imageStoreFast imageStore
#define imageLoadFast imageLoad

/* Texture format tokens -- Type explicitness required by other Graphics APIs. */
#define depth2D sampler2D
#define depth2DArray sampler2DArray
#define depth2DMS sampler2DMS
#define depth2DMSArray sampler2DMSArray
#define depthCube samplerCube
#define depthCubeArray samplerCubeArray
#define depth2DArrayShadow sampler2DArrayShadow

#define usampler2DArrayAtomic usampler2DArray
#define usampler2DAtomic usampler2D
#define usampler3DAtomic usampler3D
#define isampler2DArrayAtomic isampler2DArray
#define isampler2DAtomic isampler2D
#define isampler3DAtomic isampler3D

/* Pass through functions. */
#define imageFence(image)

/* Backend Functions. */
#define select(A, B, mask) mix(A, B, mask)

bool is_zero(vec2 A)
{
  return all(equal(A, vec2(0.0)));
}

bool is_zero(vec3 A)
{
  return all(equal(A, vec3(0.0)));
}

bool is_zero(vec4 A)
{
  return all(equal(A, vec4(0.0)));
}

/* Array syntax compatibility. */
#define float_array float[]
#define float2_array vec2[]
#define float3_array vec3[]
#define float4_array vec4[]
#define int_array int[]
#define int2_array int2[]
#define int3_array int3[]
#define int4_array int4[]
#define uint_array uint[]
#define uint2_array uint2[]
#define uint3_array uint3[]
#define uint4_array uint4[]
#define bool_array bool[]
#define bool2_array bool2[]
#define bool3_array bool3[]
#define bool4_array bool4[]
#define ARRAY_T(type) type[]
#define ARRAY_V

#define SHADER_LIBRARY_CREATE_INFO(a)
#define VERTEX_SHADER_CREATE_INFO(a)
#define FRAGMENT_SHADER_CREATE_INFO(a)
#define COMPUTE_SHADER_CREATE_INFO(a)

#define _in_sta
#define _in_end
#define _out_sta
#define _out_end
#define _inout_sta
#define _inout_end
#define _shared_sta
#define _shared_end

#define _enum_dummy /* Needed to please glslang. */
#define _enum_type(name) uint
#define _enum_decl(name) const uint
#define _enum_end _enum_dummy;
