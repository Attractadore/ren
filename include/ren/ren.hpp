#pragma once
#include <expected>
#include <glm/glm.hpp>
#include <span>
#include <utility>
#include <variant>

namespace ren {

enum class Error {
  Vulkan,
  System,
  Runtime,
  SDL2,
  Unknown,
};

template <typename T> using expected = std::expected<T, Error>;

namespace detail {

struct NullIdImpl {};

} // namespace detail

constexpr detail::NullIdImpl NullId;

#define ren_define_ptr_id(name)                                                \
  class name {                                                                 \
  public:                                                                      \
    name() = default;                                                          \
    name(detail::NullIdImpl) : name() {}                                       \
    explicit operator bool() const noexcept { return m_id; }                   \
    operator uintptr_t() const noexcept { return m_id; }                       \
                                                                               \
  private:                                                                     \
    uintptr_t m_id = 0;                                                        \
  }

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

ren_define_ptr_id(SwapchainId);
ren_define_ptr_id(SceneId);
ren_define_id(MeshId);
ren_define_id(ImageId);
ren_define_id(MaterialId);
ren_define_id(MeshInstanceId);
ren_define_id(DirectionalLightId);

#undef ren_define_id

namespace detail {

template <typename I, void (*D)(I)> class UniqueHandle {
public:
  UniqueHandle() = default;

  explicit UniqueHandle(I id) : m_id(id) {}

  UniqueHandle(UniqueHandle &&other) noexcept {
    m_id = std::exchange(other.m_id, NullId);
  }

  UniqueHandle &operator=(UniqueHandle &&other) noexcept {
    reset();
    m_id = std::exchange(other.m_id, NullId);
    return *this;
  }

  ~UniqueHandle() { reset(); }

  operator I() const noexcept { return m_id; };

  void reset() noexcept {
    if (m_id) {
      I id = std::exchange(m_id, NullId);
      D(id);
    }
  }

private:
  I m_id;
};

} // namespace detail

struct RendererDesc {
  std::span<const char *const> instance_extensions;
  unsigned adapter = 0;
};

[[nodiscard]] auto init(const RendererDesc &desc) -> expected<void>;

void quit();

[[nodiscard]] auto draw() -> expected<void>;

void destroy_swapchain(SwapchainId swapchain);

using Swapchain = detail::UniqueHandle<SwapchainId, destroy_swapchain>;

[[nodiscard]] auto get_size(SwapchainId swapchain)
    -> std::tuple<unsigned, unsigned>;

void set_size(SwapchainId swapchain, unsigned width, unsigned height);

/// Perspective projection parameters
struct PerspectiveProjection {
  /// Horizontal field of view in radians
  float hfov = glm::radians(90.0f);
};

/// Orthographic projection
struct OrthographicProjection {
  /// Width of the orthographic projection box
  float width = 1.0f;
};

/// How to calculate exposure
enum class ExposureMode {
  /// Calculate exposure using a physical camera model
  Camera,
  /// Calculate exposure automatically based on scene luminance
  Automatic,
};

struct ReinhardToneMapping {};

using ToneMappingDesc = std::variant<ReinhardToneMapping>;

/// Scene camera description
struct CameraDesc {
  /// Projection to use
  std::variant<PerspectiveProjection, OrthographicProjection> projection =
      PerspectiveProjection();
  /// Horizontal rendering resolution
  unsigned width = 1280;
  /// Vertical rendering resolution
  unsigned height = 720;
  /// Relative aperture in f-stops. Affects exposure when it's calculated based
  /// on camera parameters and depth of field
  float aperture = 16.0f;
  /// Shutter time in seconds. Affects exposure when it's calculated based
  /// on camera parameters and motion blur
  float shutter_time = 1.0f / 400.0f;
  /// Sensitivity in ISO. Ignored if exposure is not calculated based on camera
  /// parameters
  float iso = 400.0f;
  /// Exposure compensation in f-stops.
  float exposure_compensation = 0.0f;
  /// Exposure computation mode.
  ExposureMode exposure_mode = ExposureMode::Camera;
  /// This camera's position in the world.
  glm::vec3 position = {0.0f, 0.0f, 0.0f};
  /// Where this camera is facing.
  glm::vec3 forward = {1.0f, 0.0f, 0.0f};
  /// This camera's up vector.
  glm::vec3 up = {0.0f, 0.0f, 1.0f};
};

/// Mesh description
struct MeshDesc {
  /// This mesh's positions.
  std::span<const glm::vec3> positions;
  /// This mesh's normals
  std::span<const glm::vec3> normals;
  /// Optional: this mesh's tangents
  std::span<const glm::vec4> tangents;
  /// Optional: this mesh's vertex colors
  std::span<const glm::vec4> colors;
  /// Optional: this mesh's texture coordinates
  std::span<const glm::vec2> tex_coords;
  /// This mesh's indices
  std::span<const unsigned> indices;
};

/// Image storage format
enum class Format {
  Unknown,
  R8_UNORM,
  R8_SRGB,
  RG8_UNORM,
  RG8_SRGB,
  RGB8_UNORM,
  RGB8_SRGB,
  RGBA8_UNORM,
  RGBA8_SRGB,
  BGRA8_UNORM,
  BGRA8_SRGB,
  R16_UNORM,
  RG16_UNORM,
  RGB16_UNORM,
  RGBA16_UNORM,
  RGB32_SFLOAT,
  RGBA32_SFLOAT,
};

/// Image description
struct ImageDesc {
  /// Width
  unsigned width = 0;
  /// Height
  unsigned height = 0;
  /// Storage format
  Format format = Format::Unknown;
  /// Pixel data
  const void *data = nullptr;
};

/// Texture or mipmap filter
enum class Filter {
  Nearest,
  Linear,
};

/// Texture wrapping mode
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

public:
  constexpr auto operator<=>(const SamplerDesc &) const = default;
};

/// Material description
struct MaterialDesc {
  /// Color, multiplied with vertex color (if present, otherwise with
  /// [1.0, 1.0, 1.0, 1.0]) and sampled texture color (if present, otherwise
  /// with [1.0, 1.0, 1.0, 1.0]). Must be between 0 and 1
  glm::vec4 base_color_factor = {1.0f, 1.0f, 1.0f, 1.0f};
  /// Optional: color texture
  struct {
    ImageId image;
    SamplerDesc sampler;
  } base_color_texture;
  /// Metallic factor, multiplied with channel B of the metallic-roughness
  /// texture (if present, otherwise with 1.0). Must be between 0 and 1
  float metallic_factor = 1.0f;
  /// Roughness factor, multiplied with channel G of the metallic-roughness
  /// texture (if present, otherwise with 1.0). Must be between 0 and 1
  float roughness_factor = 1.0f;
  /// Optional: metallic-roughness texture
  struct {
    ImageId image;
    SamplerDesc sampler;
  } metallic_roughness_texture;
  /// Optional: normal texture
  struct {
    ImageId image;
    SamplerDesc sampler;
    /// Multiplier for sampled R and G channels
    float scale = 1.0f;
  } normal_texture;
};

/// Mesh instance description
struct MeshInstanceDesc {
  /// The mesh that will be used to render this mesh instance
  MeshId mesh;
  /// The material that will be used to render this mesh instance
  MaterialId material;
};

/// Directional light description
struct DirectionalLightDesc {
  /// This light's color. Must be between 0 and 1
  glm::vec3 color = {1.0f, 1.0f, 1.0f};
  /// This light's intensity in lux
  float illuminance = 100'000.0f;
  /// The direction this light is shining from
  glm::vec3 origin = {0.0f, 0.0f, 1.0f};
};

void destroy_scene(SceneId scene);

using Scene = detail::UniqueHandle<SceneId, destroy_scene>;

[[nodiscard]] auto create_scene(SwapchainId swapchain) -> expected<Scene>;

void set_camera(SceneId scene, const CameraDesc &desc);

void set_tone_mapping(SceneId scene, const ToneMappingDesc &desc);

[[nodiscard]] auto create_mesh(SceneId scene, const MeshDesc &desc)
    -> expected<MeshId>;

[[nodiscard]] auto create_image(SceneId scene, const ImageDesc &desc)
    -> expected<ImageId>;

[[nodiscard]] auto create_materials(SceneId scene,
                                    std::span<const MaterialDesc> descs,
                                    MaterialId *out) -> expected<void>;

[[nodiscard]] inline auto create_material(SceneId scene,
                                          const MaterialDesc &desc)
    -> expected<MaterialId> {
  MaterialId material;
  return create_materials(scene, {&desc, 1}, &material).transform([&] {
    return material;
  });
}

[[nodiscard]] auto
create_mesh_instances(SceneId scene, std::span<const MeshInstanceDesc> descs,
                      std::span<const glm::mat4x3> transforms,
                      MeshInstanceId *out) -> expected<void>;

[[nodiscard]] inline auto create_mesh_instance(SceneId scene,
                                               const MeshInstanceDesc &desc)
    -> expected<MeshInstanceId> {
  MeshInstanceId mesh_instance;
  return create_mesh_instances(scene, {&desc, 1}, {}, &mesh_instance)
      .transform([&] { return mesh_instance; });
}

[[nodiscard]] inline auto create_mesh_instance(SceneId scene,
                                               const MeshInstanceDesc &desc,
                                               const glm::mat4x3 &transform)
    -> expected<MeshInstanceId> {
  MeshInstanceId mesh_instance;
  return create_mesh_instances(scene, {&desc, 1}, {&transform, 1},
                               &mesh_instance)
      .transform([&] { return mesh_instance; });
}

void destroy_mesh_instances(SceneId scene,
                            std::span<const MeshInstanceId> mesh_instances);

inline void destroy_mesh_instance(SceneId scene, MeshInstanceId mesh_instance) {
  destroy_mesh_instances(scene, {&mesh_instance, 1});
}

void set_mesh_instance_transforms(
    SceneId scene, std::span<const MeshInstanceId> mesh_instances,
    std::span<const glm::mat4x3> transforms);

inline void set_mesh_instance_transform(SceneId scene,
                                        MeshInstanceId mesh_instance,
                                        const glm::mat4x3 &transform) {
  set_mesh_instance_transforms(scene, {&mesh_instance, 1}, {&transform, 1});
}

[[nodiscard]] auto create_directional_light(SceneId scene,
                                            const DirectionalLightDesc &desc)
    -> expected<DirectionalLightId>;

void destroy_directional_light(SceneId scene, DirectionalLightId light);

void update_directional_light(SceneId scene, DirectionalLightId light,
                              const DirectionalLightDesc &desc);

} // namespace ren
