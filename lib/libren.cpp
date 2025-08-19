#if REN_HOT_RELOAD
#include "core/DLL.hpp"
#include "ren/ren.hpp"

extern "C" REN_DLL_EXPORT ren::hot_reload::Vtbl ren_vtbl;

ren::hot_reload::Vtbl ren_vtbl = {
#define ren_vtbl_f(name) .name = &ren_export::name
    ren_vtbl_f(create_renderer),
    ren_vtbl_f(destroy_renderer),
    ren_vtbl_f(get_sdl_window_flags),
    ren_vtbl_f(set_vsync),
    ren_vtbl_f(create_swapchain),
    ren_vtbl_f(destroy_swap_chain),
    ren_vtbl_f(create_scene),
    ren_vtbl_f(destroy_scene),
    ren_vtbl_f(create_camera),
    ren_vtbl_f(destroy_camera),
    ren_vtbl_f(set_camera),
    ren_vtbl_f(set_camera_perspective_projection),
    ren_vtbl_f(set_camera_orthographic_projection),
    ren_vtbl_f(set_camera_transform),
    ren_vtbl_f(set_camera_parameters),
    ren_vtbl_f(set_exposure),
    ren_vtbl_f(create_mesh),
    ren_vtbl_f(create_image),
    ren_vtbl_f(create_material),
    ren_vtbl_f(create_mesh_instances),
    ren_vtbl_f(destroy_mesh_instances),
    ren_vtbl_f(set_mesh_instance_transforms),
    ren_vtbl_f(create_directional_light),
    ren_vtbl_f(destroy_directional_light),
    ren_vtbl_f(set_directional_light),
    ren_vtbl_f(set_environment_color),
    ren_vtbl_f(set_environment_map),
    ren_vtbl_f(delay_input),
    ren_vtbl_f(draw),
    ren_vtbl_f(unload),
    ren_vtbl_f(load),
    ren_vtbl_f(init_imgui),
    ren_vtbl_f(draw_imgui),
};
#endif
