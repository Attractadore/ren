#include "Skybox.h"
#include "Texture.glsl"

layout(location = 0) in vec2 a_position;

layout(location = 0) out vec4 f_color;

void main() {
    vec3 luminance = pc.env_luminance;
    if (!IS_NULL_DESC(pc.raw_env_map)) {
        vec4 world_pos = pc.inv_proj_view * vec4(a_position.x, a_position.y, 1.0f, 1.0f);
        vec3 v = pc.eye - world_pos.xyz / world_pos.w;
        luminance = texture_lod(pc.raw_env_map, -v, 0.0f).rgb;
    }
    float exposure = texel_fetch(pc.exposure, ivec2(0), 0).r;
    f_color = vec4(luminance * exposure, 1.0f);
}
