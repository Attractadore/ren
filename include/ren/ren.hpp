#pragma once
#include "ren/core/Arena.hpp"
#include "ren/core/NotNull.hpp"

#include <expected>
#include <glm/glm.hpp>
#include <span>

struct SDL_Window;
struct ImGuiContext;

namespace ren {

enum class Error {
  RHI,
  System,
  Runtime,
  SDL,
  InvalidFormat,
  InvalidVersion,
  IO,
  Unknown,
};

template <typename T> using expected = std::expected<T, Error>;

struct Blob {
  void *data = nullptr;
  size_t size = 0;
};

constexpr size_t MAX_NUM_MESHES = 16 * 1024;
constexpr size_t MAX_NUM_MESH_INSTANCES = 1024 * 1024;
constexpr size_t MAX_NUM_MATERIALS = 16 * 1024;
constexpr size_t MAX_NUM_DIRECTIONAL_LIGHTS = 1;

namespace detail {

#if REN_HOT_RELOAD
#define ren_export ren::hot_reload
#else
#define ren_export ren
#endif

struct NullIdImpl {};

} // namespace detail

constexpr detail::NullIdImpl NullId;

#define ren_define_id(name)                                                    \
  class name {                                                                 \
  public:                                                                      \
    name() = default;                                                          \
    name(detail::NullIdImpl) : name() {}                                       \
    explicit operator bool() const noexcept { return m_id; }                   \
    operator unsigned() const noexcept { return m_id; }                        \
                                                                               \
  private:                                                                     \
    unsigned m_id = 0;                                                         \
  }

ren_define_id(MeshId);
ren_define_id(ImageId);
ren_define_id(MaterialId);
ren_define_id(MeshInstanceId);
ren_define_id(DirectionalLightId);
ren_define_id(CameraId);

#undef ren_define_id

struct Renderer;
struct SwapChain;
struct Scene;

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
    ImageId image;
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
    ImageId image;
    SamplerDesc sampler;
    /// Controls occlusion effect strength
    float strength = 1.0f;
  } orm_texture;
  /// Optional: normal texture
  struct {
    ImageId image;
    SamplerDesc sampler;
    /// Multiplier for sampled R and G channels
    float scale = 1.0f;
  } normal_texture;
};

struct MeshInstanceCreateInfo {
  /// The mesh that will be used to render this mesh instance
  MeshId mesh;
  /// The material that will be used to render this mesh instance
  MaterialId material;
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

[[nodiscard]] auto create_renderer(Arena scratch, NotNull<Arena *> arena,
                                   const RendererInfo &info)
    -> expected<Renderer *>;

void destroy_renderer(Renderer *renderer);

[[nodiscard]] auto create_scene(Arena scratch, NotNull<Arena *> frame_arena,
                                Renderer *renderer, SwapChain *swapchain)
    -> expected<Scene *>;

void destroy_scene(Scene *scene);

void set_vsync(SwapChain *swap_chain, VSync vsync);

auto get_sdl_window_flags(Renderer *renderer) -> uint32_t;

[[nodiscard]] auto create_swapchain(Arena scratch, NotNull<Arena *> arena,
                                    Renderer *renderer, SDL_Window *window)
    -> expected<SwapChain *>;

void destroy_swap_chain(SwapChain *swap_chain);

[[nodiscard]] auto create_camera(Scene *scene) -> expected<CameraId>;

void destroy_camera(Scene *scene, CameraId camera);

/// Set active scene camera.
void set_camera(Scene *scene, CameraId camera);

void set_camera_perspective_projection(
    Scene *scene, CameraId camera, const CameraPerspectiveProjectionDesc &desc);

void set_camera_orthographic_projection(
    Scene *scene, CameraId camera,
    const CameraOrthographicProjectionDesc &desc);

void set_camera_transform(Scene *scene, CameraId camera,
                          const CameraTransformDesc &desc);

[[nodiscard]] auto create_mesh(Scene *scene, std::span<const std::byte> blob)
    -> expected<MeshId>;

[[nodiscard]] auto create_image(Scene *scene, std::span<const std::byte> blob)
    -> expected<ImageId>;

[[nodiscard]] auto create_material(Arena scratch, Scene *scene,
                                   const MaterialCreateInfo &create_info)
    -> expected<MaterialId>;

[[nodiscard]] auto
create_mesh_instances(Scene *scene,
                      std::span<const MeshInstanceCreateInfo> create_info,
                      std::span<MeshInstanceId> out) -> expected<void>;

void destroy_mesh_instances(Scene *scene,
                            std::span<const MeshInstanceId> mesh_instances);

void set_mesh_instance_transforms(
    Scene *scene, std::span<const MeshInstanceId> mesh_instances,
    std::span<const glm::mat4x3> transforms);

[[nodiscard]] auto create_directional_light(Scene *scene,
                                            const DirectionalLightDesc &desc)
    -> expected<DirectionalLightId>;

void destroy_directional_light(Scene *scene, DirectionalLightId light);

void set_directional_light(Scene *scene, DirectionalLightId light,
                           const DirectionalLightDesc &desc);

void set_environment_color(Scene *scene, const glm::vec3 &luminance);

auto set_environment_map(Arena scratch, Scene *scene, ImageId image)
    -> expected<void>;

// Call to use graphics driver low-latency APIs.
[[nodiscard]] auto delay_input(Scene *scene) -> expected<void>;

[[nodiscard]] auto draw(Arena scratch, Scene *scene, const DrawInfo &draw_info)
    -> expected<void>;

auto init_imgui(Arena scratch, Scene *scene) -> expected<void>;

void draw_imgui(Scene *scene);

} // namespace ren_export

#if REN_HOT_RELOAD

namespace ren::hot_reload {

void unload(Scene *scene);
[[nodiscard]] auto load(Arena scratch, Scene *scene) -> expected<void>;

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

[[nodiscard]] auto create_renderer(Arena scratch, NotNull<Arena *> arena,
                                   const RendererInfo &info)
    -> expected<Renderer *>;

inline void destroy_renderer(Renderer *renderer) {
  return hot_reload::vtbl_ref->destroy_renderer(renderer);
}

inline auto get_sdl_window_flags(Renderer *renderer) -> uint32_t {
  return hot_reload::vtbl_ref->get_sdl_window_flags(renderer);
}

inline auto create_swapchain(Arena scratch, NotNull<Arena *> arena,
                             Renderer *renderer, SDL_Window *window)
    -> expected<SwapChain *> {
  return hot_reload::vtbl_ref->create_swapchain(scratch, arena, renderer,
                                                window);
}

inline void destroy_swap_chain(SwapChain *swap_chain) {
  return hot_reload::vtbl_ref->destroy_swap_chain(swap_chain);
}

inline void set_vsync(SwapChain *swap_chain, VSync vsync) {
  return hot_reload::vtbl_ref->set_vsync(swap_chain, vsync);
}

inline auto create_scene(Arena scratch, NotNull<Arena *> frame_arena,
                         Renderer *renderer, SwapChain *swap_chain)
    -> expected<Scene *> {
  return hot_reload::vtbl_ref->create_scene(scratch, frame_arena, renderer,
                                            swap_chain);
}

inline void destroy_scene(Scene *scene) {
  return hot_reload::vtbl_ref->destroy_scene(scene);
}

inline auto create_camera(Scene *scene) -> expected<CameraId> {
  return hot_reload::vtbl_ref->create_camera(scene);
}

inline void destroy_camera(Scene *scene, CameraId camera) {
  return hot_reload::vtbl_ref->destroy_camera(scene, camera);
}

/// Set active scene camera.
inline void set_camera(Scene *scene, CameraId camera) {
  return hot_reload::vtbl_ref->set_camera(scene, camera);
}

inline void
set_camera_perspective_projection(Scene *scene, CameraId camera,
                                  const CameraPerspectiveProjectionDesc &desc) {
  return hot_reload::vtbl_ref->set_camera_perspective_projection(scene, camera,
                                                                 desc);
}

inline void set_camera_orthographic_projection(
    Scene *scene, CameraId camera,
    const CameraOrthographicProjectionDesc &desc) {
  return hot_reload::vtbl_ref->set_camera_orthographic_projection(scene, camera,
                                                                  desc);
}

inline void set_camera_transform(Scene *scene, CameraId camera,
                                 const CameraTransformDesc &desc) {
  return hot_reload::vtbl_ref->set_camera_transform(scene, camera, desc);
}

inline auto create_mesh(Scene *scene, std::span<const std::byte> blob)
    -> expected<MeshId> {
  return hot_reload::vtbl_ref->create_mesh(scene, blob);
}

inline auto create_image(Scene *scene, std::span<const std::byte> blob)
    -> expected<ImageId> {
  return hot_reload::vtbl_ref->create_image(scene, blob);
}

inline auto create_material(Arena scratch, Scene *scene,
                            const MaterialCreateInfo &create_info)
    -> expected<MaterialId> {
  return hot_reload::vtbl_ref->create_material(scratch, scene, create_info);
}

inline auto
create_mesh_instances(Scene *scene,
                      std::span<const MeshInstanceCreateInfo> create_info,
                      std::span<MeshInstanceId> out) -> expected<void> {
  return hot_reload::vtbl_ref->create_mesh_instances(scene, create_info, out);
}

inline void
destroy_mesh_instances(Scene *scene,
                       std::span<const MeshInstanceId> mesh_instances) {
  return hot_reload::vtbl_ref->destroy_mesh_instances(scene, mesh_instances);
}

inline void
set_mesh_instance_transforms(Scene *scene,
                             std::span<const MeshInstanceId> mesh_instances,
                             std::span<const glm::mat4x3> transforms) {
  return hot_reload::vtbl_ref->set_mesh_instance_transforms(
      scene, mesh_instances, transforms);
}

inline auto create_directional_light(Scene *scene,
                                     const DirectionalLightDesc &desc)
    -> expected<DirectionalLightId> {
  return hot_reload::vtbl_ref->create_directional_light(scene, desc);
}

inline void destroy_directional_light(Scene *scene, DirectionalLightId light) {
  return hot_reload::vtbl_ref->destroy_directional_light(scene, light);
}

inline void set_directional_light(Scene *scene, DirectionalLightId light,
                                  const DirectionalLightDesc &desc) {
  return hot_reload::vtbl_ref->set_directional_light(scene, light, desc);
}

inline void set_environment_color(Scene *scene, const glm::vec3 &luminance) {
  return hot_reload::vtbl_ref->set_environment_color(scene, luminance);
}

inline auto set_environment_map(Arena scratch, Scene *scene, ImageId image)
    -> expected<void> {
  return hot_reload::vtbl_ref->set_environment_map(scratch, scene, image);
}

inline auto delay_input(Scene *scene) -> expected<void> {
  return hot_reload::vtbl_ref->delay_input(scene);
}

[[nodiscard]] auto draw(Arena scratch, Scene *scene, const DrawInfo &draw_info)
    -> expected<void>;

inline auto init_imgui(Arena scratch, Scene *scene) -> expected<void> {
  return hot_reload::vtbl_ref->init_imgui(scratch, scene);
}

inline void draw_imgui(Scene *scene) {
  hot_reload::vtbl_ref->draw_imgui(scene);
}

} // namespace ren

#endif

namespace ren {

[[nodiscard]] auto inline create_mesh(Scene *scene, const void *blob_data,
                                      size_t blob_size) -> expected<MeshId> {
  return create_mesh(scene, std::span((const std::byte *)blob_data, blob_size));
}

[[nodiscard]] auto inline create_image(Scene *scene, const void *blob_data,
                                       size_t blob_size) -> expected<ImageId> {
  return create_image(scene,
                      std::span((const std::byte *)blob_data, blob_size));
}

[[nodiscard]] inline auto
create_mesh_instance(Scene *scene, const MeshInstanceCreateInfo &create_info)
    -> expected<MeshInstanceId> {
  MeshInstanceId mesh_instance;
  return create_mesh_instances(scene, {&create_info, 1}, {&mesh_instance, 1})
      .transform([&] { return mesh_instance; });
}

void inline destroy_mesh_instance(Scene *scene, MeshInstanceId mesh_instance) {
  destroy_mesh_instances(scene, {&mesh_instance, 1});
}

void inline set_mesh_instance_transform(Scene *scene,
                                        MeshInstanceId mesh_instance,
                                        const glm::mat4x3 &transform) {
  set_mesh_instance_transforms(scene, {&mesh_instance, 1}, {&transform, 1});
}

} // namespace ren
