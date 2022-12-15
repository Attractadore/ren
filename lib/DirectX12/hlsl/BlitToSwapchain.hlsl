Texture2D<float4> src_tex : register(t0);
SamplerState src_sampler : register(s0);

float4 main(float4 frag_pos: SV_Position): SV_Target0 {
  return src_tex.Sample(src_sampler, frag_pos.xy);
}
