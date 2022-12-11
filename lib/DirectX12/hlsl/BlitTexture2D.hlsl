#include "Texture2DBlitConfig.hlsl"

Texture2D<float4> src_tex : register(t0);
RWTexture2D<float4> dst_tex : register(u0);
SamplerState src_sampler : register(s0);

cbuffer GlobalCB : register(b0) { Texture2DBlitConfig config; };

// clang-format off
[numthreads(BlitTexture2DThreadsX, BlitTexture2DThreadsY, 1)]
void main(uint3 dtid: SV_DispatchThreadID) {
// clang-format on
  float2 uv = config.src_texel_size * (dtid.xy + 0.5f);
  dst_tex[dtid.xy] = src_tex.SampleLevel(src_sampler, uv, 0);
}
