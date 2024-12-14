#include "ImGui.h"
#include "Texture.glsl"

layout(location = A_COLOR) in vec4 a_color;
layout(location = A_UV) in vec2 a_uv;

layout(location = 0) out vec4 f_color;

void main()
{
    f_color = a_color * texture(pc.tex, a_uv);
}
