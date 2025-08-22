#include "Skybox.h"

namespace ren::sh {

SkyboxVsOutput main() {
  const vec2 VERTICES[NUM_SKYBOX_VERTICES] = {
    vec2(-1, 3),
    vec2(-1, -1),
    vec2(3, -1),
  };
  SkyboxVsOutput out;
  out.sv_position = vec4(VERTICES[gl_VertexIndex], 0.0f, 1.0f);
  out.position = VERTICES[gl_VertexIndex];
  return out;
}

}
