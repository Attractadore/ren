#include "Skybox.h"

namespace ren::sh {

[[vk::push_constant]] SkyboxArgs pc;

vec4 main(SkyboxVsOutput in) : SV_Target0 {
  vec3 luminance = pc.env_luminance;
  if (!IsNull(pc.env_map)) {
    vec4 world_pos = pc.inv_proj_view * vec4(in.position, 1.0f, 1.0f);
    vec3 v = pc.eye - world_pos.xyz / world_pos.w;
    luminance = Get(pc.env_map).SampleLevel(-v, 0).rgb;
  }
  return vec4(luminance * (*pc.exposure), 1.0f);
}

}
