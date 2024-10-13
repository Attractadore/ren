#ifndef REN_GLSL_CULLING_GLSL
#define REN_GLSL_CULLING_GLSL

#include "Culling.h"
#include "Texture.glsl"

// Assume reverse-Z, ndc_max.z contains the nearest point's depth value.
bool occlusion_cull(SampledTexture2D hi_z, vec2 ndc_min, vec3 ndc_max) {
  vec2 uv_min = vec2(ndc_min.x, -ndc_max.y) * 0.5f + 0.5f;
  vec2 uv_max = vec2(ndc_max.x, -ndc_min.y) * 0.5f + 0.5f;
  vec2 box_size = texture_size(hi_z) * (uv_max - uv_min);
  float l = max(box_size.x, box_size.y);
  float mip = ceil(log2(max(l, 1.0f)));
  float hi_z_depth = 1.0f;
  hi_z_depth = min(hi_z_depth, texture_lod(hi_z, vec2(uv_min.x, uv_min.y), mip).r);
  hi_z_depth = min(hi_z_depth, texture_lod(hi_z, vec2(uv_min.x, uv_max.y), mip).r);
  hi_z_depth = min(hi_z_depth, texture_lod(hi_z, vec2(uv_max.x, uv_min.y), mip).r);
  hi_z_depth = min(hi_z_depth, texture_lod(hi_z, vec2(uv_max.x, uv_max.y), mip).r);
  return hi_z_depth > ndc_max.z;
}

#endif // REN_GLSL_CULLING_GLSL
