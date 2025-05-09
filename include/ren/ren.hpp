#pragma once
#include <expected>
#include <glm/glm.hpp>
#include <memory>
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

struct IRenderer;
struct ISwapchain;
struct IScene;

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
    -> expected<std::unique_ptr<IRenderer>>;

struct IRenderer {
  virtual ~IRenderer() = default;
};

[[nodiscard]] auto create_scene(IRenderer &renderer, ISwapchain &swapchain)
    -> expected<std::unique_ptr<IScene>>;

enum class VSync {
  Off,
  On,
};

struct ISwapchain {
  virtual ~ISwapchain() = default;

  virtual void set_vsync(VSync vsync) = 0;
};

auto get_sdl_window_flags(IRenderer &renderer) -> uint32_t;

[[nodiscard]] auto create_swapchain(IRenderer &renderer, SDL_Window *window)
    -> expected<std::unique_ptr<ISwapchain>>;

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

struct IScene {
  virtual ~IScene() = default;

  [[nodiscard]] virtual auto create_camera() -> expected<CameraId> = 0;

  virtual void destroy_camera(CameraId camera) = 0;

  /// Set active scene camera.
  virtual void set_camera(CameraId camera) = 0;

  virtual void set_camera_perspective_projection(
      CameraId camera, const CameraPerspectiveProjectionDesc &desc) = 0;

  virtual void set_camera_orthographic_projection(
      CameraId camera, const CameraOrthographicProjectionDesc &desc) = 0;

  virtual void set_camera_transform(CameraId camera,
                                    const CameraTransformDesc &desc) = 0;

  virtual void set_camera_parameters(CameraId camera,
                                     const CameraParameterDesc &desc) = 0;

  virtual void set_exposure(const ExposureDesc &desc) = 0;

  [[nodiscard]] virtual auto create_mesh(std::span<const std::byte> blob)
      -> expected<MeshId> = 0;

  [[nodiscard]] auto create_mesh(const void *blob_data, size_t blob_size)
      -> expected<MeshId> {
    return create_mesh(std::span((const std::byte *)blob_data, blob_size));
  }

  [[nodiscard]] virtual auto create_image(std::span<const std::byte> blob)
      -> expected<ImageId> = 0;

  [[nodiscard]] auto create_image(const void *blob_data, size_t blob_size)
      -> expected<ImageId> {
    return create_image(std::span((const std::byte *)blob_data, blob_size));
  }

  [[nodiscard]] virtual auto
  create_material(const MaterialCreateInfo &create_info)
      -> expected<MaterialId> = 0;

  [[nodiscard]] virtual auto
  create_mesh_instances(std::span<const MeshInstanceCreateInfo> create_info,
                        std::span<MeshInstanceId> out) -> expected<void> = 0;

  [[nodiscard]] auto
  create_mesh_instance(const MeshInstanceCreateInfo &create_info)
      -> expected<MeshInstanceId> {
    MeshInstanceId mesh_instance;
    return create_mesh_instances({&create_info, 1}, {&mesh_instance, 1})
        .transform([&] { return mesh_instance; });
  }

  virtual void
  destroy_mesh_instances(std::span<const MeshInstanceId> mesh_instances) = 0;

  void destroy_mesh_instance(MeshInstanceId mesh_instance) {
    destroy_mesh_instances({&mesh_instance, 1});
  }

  virtual void
  set_mesh_instance_transforms(std::span<const MeshInstanceId> mesh_instances,
                               std::span<const glm::mat4x3> transforms) = 0;

  void set_mesh_instance_transform(MeshInstanceId mesh_instance,
                                   const glm::mat4x3 &transform) {
    set_mesh_instance_transforms({&mesh_instance, 1}, {&transform, 1});
  }

  [[nodiscard]] virtual auto
  create_directional_light(const DirectionalLightDesc &desc)
      -> expected<DirectionalLightId> = 0;

  virtual void destroy_directional_light(DirectionalLightId light) = 0;

  virtual void set_directional_light(DirectionalLightId light,
                                     const DirectionalLightDesc &desc) = 0;

  virtual void set_environment_color(const glm::vec3 &luminance) = 0;

  virtual auto set_environment_map(ImageId image) -> expected<void> = 0;

  // Call to use graphics driver low-latency APIs.
  [[nodiscard]] virtual auto delay_input() -> expected<void> = 0;

  [[nodiscard]] virtual auto draw() -> expected<void> = 0;
};

} // namespace ren
