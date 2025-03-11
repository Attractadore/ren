#include "Skybox.h"
#include "Texture.glsl"

layout(location = 0) out vec4 f_color;

void main() {
    float exposure = texel_fetch(pc.exposure, ivec2(0), 0).r;
    f_color = vec4(pc.env_luminance * exposure, 1.0f);
}
