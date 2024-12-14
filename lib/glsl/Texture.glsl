#ifndef REN_GLSL_TEXTURE_GLSL
#define REN_GLSL_TEXTURE_GLSL

#include "Std.h"
#include "Texture.h"

#extension GL_EXT_samplerless_texture_functions : require

#define IS_NULL_DESC(descriptor) ((descriptor).id == 0)

// clang-format off
layout(binding = SAMPLERS_SLOT) uniform sampler g_samplers[NUM_SAMPLERS];

layout(binding = TEXTURES_SLOT) uniform texture2D g_textures_2d[NUM_TEXTURES];

layout(binding = SAMPLED_TEXTURES_SLOT) uniform sampler2D g_sampled_textures_2d[NUM_SAMPLED_TEXTURES];

layout(binding = STORAGE_TEXTURES_SLOT) restrict uniform image2D g_storage_textures_2d[NUM_STORAGE_TEXTURES];
layout(binding = STORAGE_TEXTURES_SLOT) coherent restrict uniform image2D g_coherent_storage_textures_2d[NUM_STORAGE_TEXTURES];
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

ivec2 texture_size(Texture2D t) {
  return textureSize(g_textures_2d[t.id], 0);
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

#define DEFINE_STORAGE_TEXTURE_2D_IMPL(ImageType, images)                    \
  ivec2 image_size(ImageType img) { return imageSize(images[img.id]); }        \
                                                                               \
  vec4 image_load(ImageType img, ivec2 pos) {                                  \
    return imageLoad(images[img.id], pos);                                     \
  } \
  \
  void image_store(ImageType img, ivec2 pos, float data) {                     \
    imageStore(images[img.id], pos, vec4(data));                               \
  }                                                                            \
                                                                               \
  void image_store(ImageType img, ivec2 pos, vec4 data) {                      \
    imageStore(images[img.id], pos, data);                                     \
  }

DEFINE_STORAGE_TEXTURE_2D_IMPL(StorageTexture2D, g_storage_textures_2d)
DEFINE_STORAGE_TEXTURE_2D_IMPL(CoherentStorageTexture2D, g_coherent_storage_textures_2d);

#undef DEFINE_STORAGE_TEXTURE_2D_IMPL

CoherentStorageTexture2D make_coherent(StorageTexture2D img) {
  return CoherentStorageTexture2D(img.id);
}

#endif // REN_GLSL_TEXTURE_GLSL
