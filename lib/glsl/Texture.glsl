#pragma once
#if !SLANG
#include "Std.h"
#include "Texture.h"

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

#define IS_NULL_DESC(descriptor) ((descriptor).id == 0)

// clang-format off
layout(binding = SAMPLER_SLOT) uniform sampler g_samplers[MAX_NUM_SAMPLERS];

layout(binding = SRV_SLOT) uniform texture2D g_textures_2d[MAX_NUM_RESOURCES];
layout(binding = SRV_SLOT) uniform texture2D g_textures_3d[MAX_NUM_RESOURCES];

layout(binding = CIS_SLOT) uniform sampler2D g_sampled_textures_2d[MAX_NUM_RESOURCES];
layout(binding = CIS_SLOT) uniform sampler2DArray g_sampled_textures_2d_array[MAX_NUM_RESOURCES];
layout(binding = CIS_SLOT) uniform samplerCube g_sampled_textures_cube[MAX_NUM_RESOURCES];
layout(binding = CIS_SLOT) uniform sampler3D g_sampled_textures_3d[MAX_NUM_RESOURCES];

layout(binding = UAV_SLOT) restrict uniform image2D g_storage_textures_2d[MAX_NUM_RESOURCES];
layout(binding = UAV_SLOT) coherent restrict uniform image2D g_coherent_storage_textures_2d[MAX_NUM_RESOURCES];
layout(binding = UAV_SLOT) restrict uniform imageCube g_storage_textures_cube[MAX_NUM_RESOURCES];
layout(binding = UAV_SLOT) restrict uniform image3D g_storage_textures_3d[MAX_NUM_RESOURCES];
// clang-format on

#define MAKE_SAMPLER_2D(s, t) sampler2D(g_textures_2d[t.id], g_samplers[s.id])

vec4 texture(SamplerState s, Texture2D t, vec2 uv) {
  return texture(MAKE_SAMPLER_2D(s, t), uv);
}

vec4 texture_lod(SamplerState s, Texture2D t, vec2 uv, float lod) {
  return textureLod(MAKE_SAMPLER_2D(s, t), uv, lod);
}

vec4 texel_fetch(Texture2D t, ivec2 pos, int lod) {
  return texelFetch(g_textures_2d[t.id], pos, lod);
}

ivec2 texture_size(Texture2D t) { return textureSize(g_textures_2d[t.id], 0); }

vec4 texture_gather_r(SamplerState s, Texture2D t, vec2 uv) {
  return textureGather(MAKE_SAMPLER_2D(s, t), uv, 0);
}

#undef MAKE_SAMPLER_2D

vec4 texture(SampledTexture2D t, vec2 uv) {
  return texture(g_sampled_textures_2d[t.id], uv);
}

vec4 texture_lod(SampledTexture2D t, vec2 uv, float lod) {
  return textureLod(g_sampled_textures_2d[t.id], uv, lod);
}

vec4 texel_fetch(SampledTexture2D t, ivec2 pos, int lod) {
  return texelFetch(g_sampled_textures_2d[t.id], pos, lod);
}

ivec2 texture_size(SampledTexture2D t) {
  return textureSize(g_sampled_textures_2d[t.id], 0);
}

vec4 texture(SampledTexture2DArray t, vec3 uv) {
  return texture(g_sampled_textures_2d_array[t.id], uv);
}

#define texture_gather(t, p, comp) textureGather(g_sampled_textures_2d[t.id], p, comp)

vec4 texture(SampledTextureCube t, vec3 r) {
  return texture(g_sampled_textures_cube[t.id], r);
}

vec4 texture_lod(SampledTextureCube t, vec3 r, float lod) {
  return textureLod(g_sampled_textures_cube[t.id], r, lod);
}

vec4 texture_grad(SampledTextureCube t, vec3 P, vec3 dPdx, vec3 dPdy) {
  return textureGrad(g_sampled_textures_cube[t.id], P, dPdx, dPdy);
}

int texture_query_levels(SampledTextureCube t) {
  return textureQueryLevels(g_sampled_textures_cube[t.id]);
}

ivec2 texture_size(SampledTextureCube t) {
  return textureSize(g_sampled_textures_cube[t.id], 0);
}

vec4 texture(SampledTexture3D t, vec3 uv) {
  return texture(g_sampled_textures_3d[t.id], uv);
}

vec4 texture_lod(SampledTexture3D t, vec3 uv, float lod) {
  return textureLod(g_sampled_textures_3d[t.id], uv, lod);
}

vec4 texel_fetch(SampledTexture3D t, ivec3 pos, int lod) {
  return texelFetch(g_sampled_textures_3d[t.id], pos, lod);
}

ivec3 texture_size(SampledTexture3D t) {
  return textureSize(g_sampled_textures_3d[t.id], 0);
}

ivec2 image_size(StorageTexture2D img) { return imageSize(g_storage_textures_2d[img.id]); }        
                                                                             
vec4 image_load(StorageTexture2D img, ivec2 pos) {                                  
  return imageLoad(g_storage_textures_2d[img.id], pos);                                     
}                                                                            
                                                                             
vec4 image_coherent_load(StorageTexture2D img, ivec2 pos) {                         
  return imageLoad(g_coherent_storage_textures_2d[img.id], pos);                            
}                                                                            
                                                                             
void image_store(StorageTexture2D img, ivec2 pos, float data) {                     
  imageStore(g_storage_textures_2d[img.id], pos, vec4(data));                               
}                                                                            
                                                                             
void image_coherent_store(StorageTexture2D img, ivec2 pos, vec4 data) {             
  imageStore(g_coherent_storage_textures_2d[img.id], pos, data);                            
}                                                                            
                                                                             
void image_store(StorageTexture2D img, ivec2 pos, vec2 data) {                      
  imageStore(g_storage_textures_2d[img.id], pos, vec4(data, 0.0f, 0.0f));                   
}                                                                            
                                                                             
void image_store(StorageTexture2D img, ivec2 pos, vec4 data) {                      
  imageStore(g_storage_textures_2d[img.id], pos, data);                                     
}

ivec2 image_size(StorageTextureCube img) { return imageSize(g_storage_textures_cube[img.id]); }

vec4 image_load(StorageTextureCube img, ivec3 pos) {
  return imageLoad(g_storage_textures_cube[img.id], pos);
}

void image_store(StorageTextureCube img, ivec3 pos, vec3 data) {
  imageStore(g_storage_textures_cube[img.id], pos, vec4(data, 0.0f));
}

void image_store(StorageTextureCube img, ivec3 pos, vec4 data) {
  imageStore(g_storage_textures_cube[img.id], pos, data);
}

ivec3 image_size(StorageTexture3D img) { return imageSize(g_storage_textures_3d[img.id]); }        
                                                                             
vec4 image_load(StorageTexture3D img, ivec3 pos) {                                  
  return imageLoad(g_storage_textures_3d[img.id], pos);                                     
}                                                                            
                                                                             
void image_store(StorageTexture3D img, ivec3 pos, float data) {                     
  imageStore(g_storage_textures_3d[img.id], pos, vec4(data));                               
}                                                                            
                                                                             
void image_store(StorageTexture3D img, ivec3 pos, vec2 data) {                      
  imageStore(g_storage_textures_3d[img.id], pos, vec4(data, 0.0f, 0.0f));                   
}                                                                            
                                                                             
void image_store(StorageTexture3D img, ivec3 pos, vec4 data) {                      
  imageStore(g_storage_textures_3d[img.id], pos, data);                                     
}

vec3 cube_map_face_pos_to_direction(uvec2 pos, uint face, uvec2 size) {
  // uv_face = 0.5 * (uv_c / |r| + 1) =>
  // uv_c = (2 * uv_face - 1) * |r|
  vec2 uv_face = (pos + 0.5f) / size;
  vec2 uv_c = 2.0f * uv_face - 1.0f;
  float r_c = (face % 2 == 0) ? 1.0f : -1.0f;

  vec3 v;
  if (face / 2 == 0) {
    v.zyx = vec3(r_c * -uv_c.x, -uv_c.y, r_c);
  } else if (face / 2 == 1) {
    v.xzy = vec3(uv_c.x, r_c * uv_c.y, r_c);
  } else {
    v.xyz = vec3(r_c * uv_c.x, -uv_c.y, r_c);
  }

  return v;
}

vec2 direction_to_equirectangular_uv(vec3 r) {
  float phi = atan(r.y, r.x);
  float theta = acos(r.z / length(r));
  return vec2(phi / TWO_PI, theta / PI);
}

float reduce_quad_checkered_min_max(uint x, uint y, vec4 v) {
  float minv = min(min(v.x, v.y), min(v.z, v.w));
  float maxv = max(max(v.x, v.y), max(v.z, v.w));
  return maxv;
  return (x + y) % 2 == 0 ? minv : maxv;
}
#endif
