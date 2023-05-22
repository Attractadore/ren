#include "interface.hlsl"
#include "memory.hlsl"
#include "postprocess.hlsl"
#include "postprocess_interface.hlsl"

[[vk::binding(STORAGE_TEXTURES_SLOT, PERSISTENT_SET)]] RWTexture2D<float4> g_textures[NUM_STORAGE_TEXTURES];

[[vk::push_constant]] ReinhardPushConstants g_pcs;

[numthreads(REINHARD_THREADS_X, REINHARD_THREADS_Y, 1)]
void main(uint3 dtid: SV_DispatchThreadID) {
  RWTexture2D<float4> tex = g_textures[g_pcs.tex];

  uint2 size;
  tex.GetDimensions(size.x, size.y);
  if (any(dtid.xy >= size)) {
    return;
  }

  float3 color = tex[dtid.xy].rgb;
  float exposure = ptr_load<float>(g_pcs.exposure_ptr);

  color *= exposure;
  float luminance = get_luminance(color);
  color *= luminance / (1.0f + luminance);

  tex[dtid.xy].rgb = color;
}
