#include "Skybox.h"

layout(location = 0) out vec2 a_position;

void main() {
  const vec2 VERTICES[NUM_SKYBOX_VERTICES] = {
    vec2(-1, 3),
    vec2(-1, -1),
    vec2(3, -1),
  };
  gl_Position = vec4(VERTICES[gl_VertexIndex], 0.0f, 1.0f);
  a_position = VERTICES[gl_VertexIndex];
}
