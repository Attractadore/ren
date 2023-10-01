#include "ImGuiPass.glsl"
#include "Textures.glsl"

TEXTURES;

PUSH_CONSTANTS GLSL_IMGUI_CONSTANTS pc;

IN_BLOCK FS_IN v_in;

OUT vec4 f_color;

void main()
{
    f_color = v_in.color * texture(g_textures2d[pc.tex], v_in.tex_coord);
}
