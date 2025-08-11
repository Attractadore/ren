#pragma once
#include <expected>
#include <glm/glm.hpp>
#include <span>

struct SDL_Window;

namespace ren {

enum class Error {
  RHI,
  System,
  Runtime,
  SDL2,
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

[[nodiscard]] auto create_renderer(const RendererInfo &info)
    -> expected<Renderer *>;

void destroy_renderer(Renderer *renderer);

[[nodiscard]] auto create_scene(Renderer *renderer, SwapChain *swapchain)
    -> expected<Scene *>;

void destroy_scene(Scene *scene);

enum class VSync {
  Off,
  On,
};

void set_vsync(SwapChain *swap_chain, VSync vsync);

auto get_sdl_window_flags(Renderer *renderer) -> uint32_t;

[[nodiscard]] auto create_swapchain(Renderer *renderer, SDL_Window *window)
    -> expected<SwapChain *>;

void destroy_swap_chain(SwapChain *swap_chain);

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

/// Camera physical parameters and settings descriptor.
struct CameraParameterDesc {
  /// Relative aperture in f-stops.
  float aperture = 16.0f;
  /// Shutter time in seconds.
  float shutter_time = 1.0f / 400.0f;
  /// Sensitivity in ISO. Ignored if exposure is not calculated from camera
  /// parameters.
  float iso = 400.0f;
};

/// Exposure algorithm.
enum class ExposureMode {
  /// Calculate exposure using a physical camera model.
  Camera,
  /// Calculate exposure automatically based on scene luminance.
  Automatic,
};

struct ExposureDesc {
  ExposureMode mode = ExposureMode::Automatic;
  /// Exposure compensation in f-stops.
  float ec = 0.0f;
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

void set_camera_parameters(Scene *scene, CameraId camera,
                           const CameraParameterDesc &desc);

void set_exposure(Scene *scene, const ExposureDesc &desc);

[[nodiscard]] auto create_mesh(Scene *scene, std::span<const std::byte> blob)
    -> expected<MeshId>;

[[nodiscard]] auto inline create_mesh(Scene *scene, const void *blob_data,
                                      size_t blob_size) -> expected<MeshId> {
  return create_mesh(scene, std::span((const std::byte *)blob_data, blob_size));
}

[[nodiscard]] auto create_image(Scene *scene, std::span<const std::byte> blob)
    -> expected<ImageId>;

[[nodiscard]] auto inline create_image(Scene *scene, const void *blob_data,
                                       size_t blob_size) -> expected<ImageId> {
  return create_image(scene,
                      std::span((const std::byte *)blob_data, blob_size));
}

[[nodiscard]] auto create_material(Scene *scene,
                                   const MaterialCreateInfo &create_info)
    -> expected<MaterialId>;

[[nodiscard]] auto
create_mesh_instances(Scene *scene,
                      std::span<const MeshInstanceCreateInfo> create_info,
                      std::span<MeshInstanceId> out) -> expected<void>;

[[nodiscard]] inline auto
create_mesh_instance(Scene *scene, const MeshInstanceCreateInfo &create_info)
    -> expected<MeshInstanceId> {
  MeshInstanceId mesh_instance;
  return create_mesh_instances(scene, {&create_info, 1}, {&mesh_instance, 1})
      .transform([&] { return mesh_instance; });
}

void destroy_mesh_instances(Scene *scene,
                            std::span<const MeshInstanceId> mesh_instances);

void inline destroy_mesh_instance(Scene *scene, MeshInstanceId mesh_instance) {
  destroy_mesh_instances(scene, {&mesh_instance, 1});
}

void set_mesh_instance_transforms(
    Scene *scene, std::span<const MeshInstanceId> mesh_instances,
    std::span<const glm::mat4x3> transforms);

void inline set_mesh_instance_transform(Scene *scene,
                                        MeshInstanceId mesh_instance,
                                        const glm::mat4x3 &transform) {
  set_mesh_instance_transforms(scene, {&mesh_instance, 1}, {&transform, 1});
}

[[nodiscard]] auto create_directional_light(Scene *scene,
                                            const DirectionalLightDesc &desc)
    -> expected<DirectionalLightId>;

void destroy_directional_light(Scene *scene, DirectionalLightId light);

void set_directional_light(Scene *scene, DirectionalLightId light,
                           const DirectionalLightDesc &desc);

void set_environment_color(Scene *scene, const glm::vec3 &luminance);

auto set_environment_map(Scene *scene, ImageId image) -> expected<void>;

// Call to use graphics driver low-latency APIs.
[[nodiscard]] auto delay_input(Scene *scene) -> expected<void>;

[[nodiscard]] auto draw(Scene *scene) -> expected<void>;

} // namespace ren
