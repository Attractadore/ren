#pragma once
#include "ren.h"

#include <glm/vec3.hpp>

#include <cassert>
#include <concepts>
#include <cstring>
#include <memory>
#include <span>
#include <variant>

struct RenDevice {
  ~RenDevice() = delete;
};

struct RenSwapchain {
  ~RenSwapchain() = delete;
};

struct RenScene {
  ~RenScene() = delete;
};

namespace ren {
inline namespace v0 {

struct Scene;

namespace detail {

template <typename T>
concept scoped_enum =
    std::is_enum_v<T> and not std::convertible_to<T, std::underlying_type_t<T>>;

template <scoped_enum H> class SharedHandle;

template <typename H> constexpr void (Scene::*HandleDestroy)(H);

template <typename H> constexpr void *from_handle(H handle) {
  return reinterpret_cast<void *>(static_cast<uintptr_t>(handle));
}

template <typename H> constexpr H to_handle(void *handle) {
  return static_cast<H>(reinterpret_cast<uintptr_t>(handle));
}

template <typename H> struct HandleDeleter {
  Scene *scene = nullptr;

  void operator()(void *handle) {
    if (handle) {
      assert(scene);
      (scene->*HandleDestroy<H>)(to_handle<H>(handle));
    }
  }
};

template <scoped_enum H> class UniqueHandle {
  friend class SharedHandle<H>;

  std::unique_ptr<void, HandleDeleter<H>> m_holder;

public:
  UniqueHandle() = default;
  UniqueHandle(Scene *scene, H handle)
      : m_holder(from_handle(handle), HandleDeleter<H>{scene}) {}

  H get() const { return to_handle<H>(m_holder.get()); }

  const Scene *get_scene() const { return m_holder.get_deleter().scene; }

  Scene *get_scene() { return m_holder.get_deleter().scene; }
};

template <scoped_enum H> class SharedHandle {
  std::shared_ptr<void> m_holder;

public:
  SharedHandle(UniqueHandle<H> unique) : m_holder(std::move(unique.m_holder)) {}

  H get() const { reinterpret_cast<H>(m_holder.get()); }

  const Scene *get_scene() const {
    return get_deleter<HandleDeleter<H>>(m_holder)->scene;
  }

  Scene *get_scene() { return get_deleter<HandleDeleter<H>>(m_holder)->scene; }
};

template <class... Ts> struct overload_set : Ts... {
  overload_set(Ts... fs) : Ts(std::move(fs))... {}
  using Ts::operator()...;
};

} // namespace detail

struct Device;
struct DeviceDeleter {
  void operator()(Device *device) const noexcept {
    ren_DestroyDevice(reinterpret_cast<RenDevice *>(device));
  }
};
using UniqueDevice = std::unique_ptr<Device, DeviceDeleter>;
using SharedDevice = std::shared_ptr<Device>;

struct Swapchain;
struct SwapchainDeleter {
  void operator()(Swapchain *swapchain) const noexcept {
    ren_DestroySwapchain(reinterpret_cast<RenSwapchain *>(swapchain));
  }
};
using UniqueSwapchain = std::unique_ptr<Swapchain, SwapchainDeleter>;
using SharedSwapchain = std::shared_ptr<Swapchain>;

struct SceneDeleter {
  void operator()(Scene *scene) const noexcept {
    ren_DestroyScene(reinterpret_cast<RenScene *>(scene));
  }
};
using UniqueScene = std::unique_ptr<Scene, SceneDeleter>;
using SharedScene = std::shared_ptr<Scene>;

enum class Mesh : RenMesh;
using UniqueMesh = detail::UniqueHandle<Mesh>;
using SharedMesh = detail::SharedHandle<Mesh>;

enum class Material : RenMaterial;
using UniqueMaterial = detail::UniqueHandle<Material>;
using SharedMaterial = detail::SharedHandle<Material>;

enum class Model : RenModel;
using UniqueModel = detail::UniqueHandle<Model>;
using SharedModel = detail::SharedHandle<Model>;

struct Device : RenDevice {
  UniqueScene create_scene() {
    return {reinterpret_cast<Scene *>(ren_CreateScene(this)), SceneDeleter()};
  }
};

struct Swapchain : RenSwapchain {
  void set_size(unsigned width, unsigned height) {
    ren_SetSwapchainSize(this, width, height);
  }
};

using PerspectiveCameraDesc = RenPerspectiveCameraDesc;
using OrthographicCameraDesc = RenOrthographicCameraDesc;

struct CameraDesc {
  std::variant<PerspectiveCameraDesc, OrthographicCameraDesc> projection_desc;
  glm::vec3 position;
  glm::vec3 forward;
  glm::vec3 up;
};

struct CameraRef {
  RenScene *m_scene = nullptr;

public:
  CameraRef() = default;
  CameraRef(Scene *scene) : m_scene(reinterpret_cast<RenScene *>(scene)) {}

  Scene *get_scene() { return reinterpret_cast<Scene *>(m_scene); };
  const Scene *get_scene() const {
    return reinterpret_cast<const Scene *>(m_scene);
  };

  void config(const CameraDesc &desc) {
    RenCameraDesc c_desc = {};
    std::visit(
        detail::overload_set{[&](const PerspectiveCameraDesc &perspective) {
                               c_desc.type = REN_PROJECTION_PERSPECTIVE;
                               c_desc.perspective = perspective;
                             },
                             [&](const OrthographicCameraDesc &ortho) {
                               c_desc.type = REN_PROJECTION_ORTHOGRAPHIC;
                               c_desc.orthographic = ortho;
                             }},
        desc.projection_desc);
    std::memcpy(c_desc.position, &desc.position, sizeof(desc.position));
    std::memcpy(c_desc.forward, &desc.forward, sizeof(desc.forward));
    std::memcpy(c_desc.up, &desc.up, sizeof(desc.up));
    ren_SetSceneCamera(m_scene, &c_desc);
  }
};

struct MeshDesc {
  std::span<const glm::vec3> positions;
  std::span<const glm::vec3> colors;
  std::span<const unsigned> indices;
};

struct ConstMaterialAlbedo {
  glm::vec3 color;
};

struct VertexMaterialAlbedo {};

struct MaterialDesc {
  std::variant<ConstMaterialAlbedo, VertexMaterialAlbedo> albedo;
};

struct ModelDesc {
  Mesh mesh;
  Material material;
};

struct Scene : RenScene {
  void set_swapchain(Swapchain *swapchain) {
    ren_SetSceneSwapchain(this, swapchain);
  }

  CameraRef get_camera() { return {this}; }

  void set_output_size(unsigned width, unsigned height) {
    ren_SetSceneOutputSize(this, width, height);
  }

  void draw() { ren_DrawScene(this); }

  Mesh create_mesh(const MeshDesc &desc) {
    assert(not desc.positions.empty());
    assert(desc.colors.empty() or desc.colors.size() == desc.positions.size());
    assert(not desc.indices.empty());
    RenMeshDesc c_desc = {
        .num_vertices = unsigned(desc.positions.size()),
        .num_indices = unsigned(desc.indices.size()),
        .positions = reinterpret_cast<const float *>(desc.positions.data()),
        .colors = desc.colors.empty()
                      ? nullptr
                      : reinterpret_cast<const float *>(desc.colors.data()),
        .indices = desc.indices.data(),
    };
    return static_cast<Mesh>(ren_CreateMesh(this, &c_desc));
  }

  void destroy_mesh(Mesh mesh) {
    ren_DestroyMesh(this, static_cast<RenMesh>(mesh));
  }

  UniqueMesh create_unique_mesh(const MeshDesc &desc) {
    return {this, create_mesh(desc)};
  }

  SharedMesh create_shared_mesh(const MeshDesc &desc) {
    return create_unique_mesh(desc);
  }

  Material create_material(const MaterialDesc &desc) {
    RenMaterialDesc c_desc = {};
    std::visit(
        detail::overload_set{[&](const ConstMaterialAlbedo &albedo) {
                               c_desc.albedo_type = REN_MATERIAL_ALBEDO_CONST;
                               std::memcpy(&c_desc.albedo_color, &albedo.color,
                                           sizeof(albedo.color));
                             },
                             [&](const VertexMaterialAlbedo &albedo) {
                               c_desc.albedo_type = REN_MATERIAL_ALBEDO_VERTEX;
                             }},
        desc.albedo);
    return static_cast<Material>(ren_CreateMaterial(this, &c_desc));
  }

  void destroy_material(Material material) {
    ren_DestroyMaterial(this, static_cast<RenMaterial>(material));
  }

  UniqueMaterial create_unique_material(const MaterialDesc &desc) {
    return {this, create_material(desc)};
  }

  SharedMaterial create_shared_material(const MaterialDesc &desc) {
    return create_unique_material(desc);
  }

  Model create_model(const ModelDesc &desc) {
    RenModelDesc c_desc = {
        .mesh = static_cast<RenMesh>(desc.mesh),
        .material = static_cast<RenMaterial>(desc.material),
    };
    return static_cast<Model>(ren_CreateModel(this, &c_desc));
  }

  void destroy_model(Model model) {
    ren_DestroyModel(this, static_cast<RenModel>(model));
  }

  UniqueModel create_unique_model(const ModelDesc &desc) {
    return {this, create_model(desc)};
  }

  SharedModel create_shared_model(const ModelDesc &desc) {
    return create_unique_model(desc);
  }
};

namespace detail {

template <>
inline constexpr void (Scene::*HandleDestroy<Mesh>)(Mesh) =
    &Scene::destroy_mesh;
template <>
inline constexpr void (Scene::*HandleDestroy<Material>)(Material) =
    &Scene::destroy_material;
template <>
inline constexpr void (Scene::*HandleDestroy<Model>)(Model) =
    &Scene::destroy_model;

} // namespace detail

} // namespace v0
} // namespace ren
