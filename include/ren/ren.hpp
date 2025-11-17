#pragma once
#include "ren/core/Arena.hpp"
#include "ren/core/GenIndex.hpp"
#include "ren/core/NotNull.hpp"
#include "ren/core/Span.hpp"

#include <glm/glm.hpp>

struct SDL_Window;
struct ImGuiContext;

#if REN_HOT_RELOAD
#define ren_export ren::hot_reload
#else
#define ren_export ren
#endif

namespace ren {

struct Blob {
  void *data = nullptr;
  size_t size = 0;
};

constexpr size_t MAX_NUM_MESHES = 16 * 1024;
constexpr size_t MAX_NUM_MESH_INSTANCES = 1024 * 1024;
constexpr size_t MAX_NUM_MATERIALS = 16 * 1024;
constexpr size_t MAX_NUM_DIRECTIONAL_LIGHTS = 1;

struct Renderer;
struct SwapChain;
struct Scene;
struct Camera;
struct Mesh;
struct MeshInstance;
struct Material;
struct Image;
struct DirectionalLight;

constexpr unsigned DEFAULT_ADAPTER = -1;

enum class RendererType {
  Default,
  Headless,
};

struct RendererInfo {
  unsigned adapter = DEFAULT_ADAPTER;
  RendererType type = RendererType::Default;
};

enum class VSync {
  Off,
  On,
};

/// Camera perspective projection descriptor.
struct CameraPerspectiveProjectionDesc {
  /// Horizontal field-of-view in radians.
  float hfov = glm::radians(90.0f);
  /// Near plane.
  float near = 0.01f;
};

/// Camera orthographic projection descriptor.
struct CameraOrthographicProjectionDesc {
  /// Box width in units.
  float width = 1.0f;
  /// Near plane.
  float near = 0.01f;
  /// Far plane.
  float far = 100.0f;
};

/// Camera transform descriptor.
struct CameraTransformDesc {
  glm::vec3 position;
  glm::vec3 forward = {1.0f, 0.0f, 0.0f};
  glm::vec3 up = {0.0f, 0.0f, 1.0f};
};

/// Texture or mipmap filter.
enum class Filter {
  Nearest,
  Linear,
};

/// Texture wrapping mode.
enum WrappingMode {
  Repeat,
  MirroredRepeat,
  ClampToEdge,
};

/// Texture sampler state description
struct SamplerDesc {
  /// Magnification filter
  Filter mag_filter = Filter::Linear;
  /// Minification filter
  Filter min_filter = Filter::Linear;
  /// Mipmap filter
  Filter mipmap_filter = Filter::Linear;
  /// U coordinate wrapping mode
  WrappingMode wrap_u = WrappingMode::Repeat;
  /// V coordinate wrapping mode
  WrappingMode wrap_v = WrappingMode::Repeat;
};

/// Material description
struct MaterialCreateInfo {
  /// Color, multiplied with vertex color (if present, otherwise with
  /// [1.0, 1.0, 1.0, 1.0]) and sampled texture color (if present, otherwise
  /// with [1.0, 1.0, 1.0, 1.0]). Must be between 0 and 1
  glm::vec4 base_color_factor = {1.0f, 1.0f, 1.0f, 1.0f};
  /// Optional: color texture
  struct {
    Handle<Image> image;
    SamplerDesc sampler;
  } base_color_texture;
  /// Roughness factor, multiplied with channel G of the metallic-roughness
  /// texture (if present, otherwise with 1.0). Must be between 0 and 1
  float roughness_factor = 1.0f;
  /// Metallic factor, multiplied with channel B of the metallic-roughness
  /// texture (if present, otherwise with 1.0). Must be between 0 and 1
  float metallic_factor = 1.0f;
  /// Optional: occlusion-roughness-metallic texture
  struct {
    Handle<Image> image;
    SamplerDesc sampler;
    /// Controls occlusion effect strength
    float strength = 1.0f;
  } orm_texture;
  /// Optional: normal texture
  struct {
    Handle<Image> image;
    SamplerDesc sampler;
    /// Multiplier for sampled R and G channels
    float scale = 1.0f;
  } normal_texture;
};

struct MeshInstanceCreateInfo {
  /// The mesh that will be used to render this mesh instance
  Handle<Mesh> mesh;
  /// The material that will be used to render this mesh instance
  Handle<Material> material;
};

/// Directional light descriptor
struct DirectionalLightDesc {
  /// This light's color. Must be between 0 and 1.
  glm::vec3 color = {1.0f, 1.0f, 1.0f};
  /// This light's intensity in lux.
  float illuminance = 100'000.0f;
  /// The direction this light is shining from.
  glm::vec3 origin = {0.0f, 0.0f, 1.0f};
};

struct DrawInfo {
  float delta_time = 0.0f;
};

} // namespace ren

namespace ren_export {

[[nodiscard]] Renderer *create_renderer(NotNull<Arena *> arena,
                                        const RendererInfo &info);

void destroy_renderer(Renderer *renderer);

auto get_sdl_window_flags(Renderer *renderer) -> uint32_t;

[[nodiscard]] SwapChain *create_swapchain(NotNull<Arena *> arena,
                                          Renderer *renderer,
                                          SDL_Window *window);

void destroy_swap_chain(SwapChain *swap_chain);

void set_vsync(SwapChain *swap_chain, VSync vsync);

[[nodiscard]] Scene *create_scene(NotNull<Arena *> arena, Renderer *renderer,
                                  SwapChain *swapchain);

void destroy_scene(Scene *scene);

[[nodiscard]] Handle<Camera> create_camera(Scene *scene);

void destroy_camera(Scene *scene, Handle<Camera> camera);

/// Set active scene camera.
void set_camera(Scene *scene, Handle<Camera> camera);

void set_camera_perspective_projection(
    Scene *scene, Handle<Camera> camera,
    const CameraPerspectiveProjectionDesc &desc);

void set_camera_orthographic_projection(
    Scene *scene, Handle<Camera> camera,
    const CameraOrthographicProjectionDesc &desc);

void set_camera_transform(Scene *scene, Handle<Camera> camera,
                          const CameraTransformDesc &desc);

[[nodiscard]] auto create_mesh(NotNull<Arena *> frame_arena, Scene *scene,
                               Span<const std::byte> blob) -> Handle<Mesh>;

[[nodiscard]] auto create_image(NotNull<Arena *> frame_arena, Scene *scene,
                                Span<const std::byte> blob) -> Handle<Image>;

[[nodiscard]] auto create_material(NotNull<Arena *> frame_arena, Scene *scene,
                                   const MaterialCreateInfo &create_info)
    -> Handle<Material>;

void create_mesh_instances(NotNull<Arena *> frame_arena, Scene *scene,
                           Span<const MeshInstanceCreateInfo> create_info,
                           Span<Handle<MeshInstance>> out);

void destroy_mesh_instances(NotNull<Arena *> frame_arena, Scene *scene,
                            Span<const Handle<MeshInstance>> mesh_instances);

void set_mesh_instance_transforms(
    NotNull<Arena *> frame_arena, Scene *scene,
    Span<const Handle<MeshInstance>> mesh_instances,
    Span<const glm::mat4x3> transforms);

[[nodiscard]] auto create_directional_light(Scene *scene,
                                            const DirectionalLightDesc &desc)
    -> Handle<DirectionalLight>;

void destroy_directional_light(Scene *scene, Handle<DirectionalLight> light);

void set_directional_light(Scene *scene, Handle<DirectionalLight> light,
                           const DirectionalLightDesc &desc);

void set_environment_color(Scene *scene, const glm::vec3 &luminance);

void set_environment_map(Scene *scene, Handle<Image> image);

// Call to use graphics driver low-latency APIs.
void delay_input(Scene *scene);

void draw(Scene *scene, const DrawInfo &draw_info);

void init_imgui(NotNull<Arena *> frame_arena, Scene *scene);

void draw_imgui(Scene *scene);

} // namespace ren_export

#if REN_HOT_RELOAD

namespace ren::hot_reload {

void unload(Scene *scene);
[[nodiscard]] bool load(Scene *scene);

struct Vtbl {
#define ren_vtbl_f(name) decltype(ren_export::name) *name
  ren_vtbl_f(create_renderer);
  ren_vtbl_f(destroy_renderer);
  ren_vtbl_f(get_sdl_window_flags);
  ren_vtbl_f(set_vsync);
  ren_vtbl_f(create_swapchain);
  ren_vtbl_f(destroy_swap_chain);
  ren_vtbl_f(create_scene);
  ren_vtbl_f(destroy_scene);
  ren_vtbl_f(create_camera);
  ren_vtbl_f(destroy_camera);
  ren_vtbl_f(set_camera);
  ren_vtbl_f(set_camera_perspective_projection);
  ren_vtbl_f(set_camera_orthographic_projection);
  ren_vtbl_f(set_camera_transform);
  ren_vtbl_f(create_mesh);
  ren_vtbl_f(create_image);
  ren_vtbl_f(create_material);
  ren_vtbl_f(create_mesh_instances);
  ren_vtbl_f(destroy_mesh_instances);
  ren_vtbl_f(set_mesh_instance_transforms);
  ren_vtbl_f(create_directional_light);
  ren_vtbl_f(destroy_directional_light);
  ren_vtbl_f(set_directional_light);
  ren_vtbl_f(set_environment_color);
  ren_vtbl_f(set_environment_map);
  ren_vtbl_f(delay_input);
  ren_vtbl_f(draw);
  ren_vtbl_f(unload);
  ren_vtbl_f(load);
  ren_vtbl_f(init_imgui);
  ren_vtbl_f(draw_imgui);
#undef ren_vtbl_f
};

extern const ren::hot_reload::Vtbl *vtbl_ref;

} // namespace ren::hot_reload

namespace ren {

[[nodiscard]] Renderer *create_renderer(NotNull<Arena *> arena,
                                        const RendererInfo &info);

inline void destroy_renderer(Renderer *renderer) {
  return hot_reload::vtbl_ref->destroy_renderer(renderer);
}

inline auto get_sdl_window_flags(Renderer *renderer) -> uint32_t {
  return hot_reload::vtbl_ref->get_sdl_window_flags(renderer);
}

inline SwapChain *create_swapchain(NotNull<Arena *> arena, Renderer *renderer,
                                   SDL_Window *window) {
  return hot_reload::vtbl_ref->create_swapchain(arena, renderer, window);
}

inline void destroy_swap_chain(SwapChain *swap_chain) {
  return hot_reload::vtbl_ref->destroy_swap_chain(swap_chain);
}

inline void set_vsync(SwapChain *swap_chain, VSync vsync) {
  return hot_reload::vtbl_ref->set_vsync(swap_chain, vsync);
}

inline Scene *create_scene(NotNull<Arena *> arena, Renderer *renderer,
                           SwapChain *swap_chain) {
  return hot_reload::vtbl_ref->create_scene(arena, renderer, swap_chain);
}

inline void destroy_scene(Scene *scene) {
  return hot_reload::vtbl_ref->destroy_scene(scene);
}

inline auto create_camera(Scene *scene) {
  return hot_reload::vtbl_ref->create_camera(scene);
}

inline void destroy_camera(Scene *scene, Handle<Camera> camera) {
  return hot_reload::vtbl_ref->destroy_camera(scene, camera);
}

/// Set active scene camera.
inline void set_camera(Scene *scene, Handle<Camera> camera) {
  return hot_reload::vtbl_ref->set_camera(scene, camera);
}

inline void
set_camera_perspective_projection(Scene *scene, Handle<Camera> camera,
                                  const CameraPerspectiveProjectionDesc &desc) {
  return hot_reload::vtbl_ref->set_camera_perspective_projection(scene, camera,
                                                                 desc);
}

inline void set_camera_orthographic_projection(
    Scene *scene, Handle<Camera> camera,
    const CameraOrthographicProjectionDesc &desc) {
  return hot_reload::vtbl_ref->set_camera_orthographic_projection(scene, camera,
                                                                  desc);
}

inline void set_camera_transform(Scene *scene, Handle<Camera> camera,
                                 const CameraTransformDesc &desc) {
  return hot_reload::vtbl_ref->set_camera_transform(scene, camera, desc);
}

inline auto create_mesh(NotNull<Arena *> frame_arena, Scene *scene,
                        Span<const std::byte> blob) -> Handle<Mesh> {
  return hot_reload::vtbl_ref->create_mesh(frame_arena, scene, blob);
}

inline auto create_image(NotNull<Arena *> frame_arena, Scene *scene,
                         Span<const std::byte> blob) -> Handle<Image> {
  return hot_reload::vtbl_ref->create_image(frame_arena, scene, blob);
}

inline auto create_material(NotNull<Arena *> frame_arena, Scene *scene,
                            const MaterialCreateInfo &create_info)
    -> Handle<Material> {
  return hot_reload::vtbl_ref->create_material(frame_arena, scene, create_info);
}

inline void
create_mesh_instances(NotNull<Arena *> frame_arena, Scene *scene,
                      Span<const MeshInstanceCreateInfo> create_info,
                      Span<Handle<MeshInstance>> out) {
  return hot_reload::vtbl_ref->create_mesh_instances(frame_arena, scene,
                                                     create_info, out);
}

inline void
destroy_mesh_instances(NotNull<Arena *> frame_arena, Scene *scene,
                       Span<const Handle<MeshInstance>> mesh_instances) {
  return hot_reload::vtbl_ref->destroy_mesh_instances(frame_arena, scene,
                                                      mesh_instances);
}

inline void
set_mesh_instance_transforms(NotNull<Arena *> frame_arena, Scene *scene,
                             Span<const Handle<MeshInstance>> mesh_instances,
                             Span<const glm::mat4x3> transforms) {
  return hot_reload::vtbl_ref->set_mesh_instance_transforms(
      frame_arena, scene, mesh_instances, transforms);
}

inline auto create_directional_light(Scene *scene,
                                     const DirectionalLightDesc &desc)
    -> Handle<DirectionalLight> {
  return hot_reload::vtbl_ref->create_directional_light(scene, desc);
}

inline void destroy_directional_light(Scene *scene,
                                      Handle<DirectionalLight> light) {
  return hot_reload::vtbl_ref->destroy_directional_light(scene, light);
}

inline void set_directional_light(Scene *scene, Handle<DirectionalLight> light,
                                  const DirectionalLightDesc &desc) {
  return hot_reload::vtbl_ref->set_directional_light(scene, light, desc);
}

inline void set_environment_color(Scene *scene, const glm::vec3 &luminance) {
  return hot_reload::vtbl_ref->set_environment_color(scene, luminance);
}

inline void set_environment_map(Scene *scene, Handle<Image> image) {
  return hot_reload::vtbl_ref->set_environment_map(scene, image);
}

inline void delay_input(Scene *scene) {
  return hot_reload::vtbl_ref->delay_input(scene);
}

void draw(Scene *scene, const DrawInfo &draw_info);

inline void init_imgui(NotNull<Arena *> frame_arena, Scene *scene) {
  hot_reload::vtbl_ref->init_imgui(frame_arena, scene);
}

inline void draw_imgui(Scene *scene) {
  hot_reload::vtbl_ref->draw_imgui(scene);
}

} // namespace ren

#endif

namespace ren {

[[nodiscard]] auto inline create_mesh(NotNull<Arena *> frame_arena,
                                      Scene *scene, const void *blob_data,
                                      size_t blob_size) -> Handle<Mesh> {
  return create_mesh(frame_arena, scene,
                     Span((const std::byte *)blob_data, blob_size));
}

[[nodiscard]] auto inline create_image(NotNull<Arena *> frame_arena,
                                       Scene *scene, const void *blob_data,
                                       size_t blob_size) -> Handle<Image> {
  return create_image(frame_arena, scene,
                      Span((const std::byte *)blob_data, blob_size));
}

[[nodiscard]] inline auto
create_mesh_instance(NotNull<Arena *> frame_arena, Scene *scene,
                     const MeshInstanceCreateInfo &create_info)
    -> Handle<MeshInstance> {
  Handle<MeshInstance> mesh_instance;
  create_mesh_instances(frame_arena, scene, {&create_info, 1},
                        {&mesh_instance, 1});
  return mesh_instance;
}

void inline destroy_mesh_instance(NotNull<Arena *> frame_arena, Scene *scene,
                                  Handle<MeshInstance> mesh_instance) {
  destroy_mesh_instances(frame_arena, scene, {&mesh_instance, 1});
}

void inline set_mesh_instance_transform(NotNull<Arena *> frame_arena,
                                        Scene *scene,
                                        Handle<MeshInstance> mesh_instance,
                                        const glm::mat4x3 &transform) {
  set_mesh_instance_transforms(frame_arena, scene, {&mesh_instance, 1},
                               {&transform, 1});
}

} // namespace ren
