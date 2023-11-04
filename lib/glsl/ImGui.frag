#include "ImGuiPass.glsl"
#include "Textures.glsl"

TEXTURES;

PUSH_CONSTANTS GLSL_IMGUI_CONSTANTS pc;

layout(location = V_COLOR) in vec4 v_color;
layout(location = V_UV) in vec2 v_uv;

layout(location = 0) out vec4 f_color;

void main()
{
    f_color = v_color * texture(g_textures2d[pc.tex], v_uv);
}
