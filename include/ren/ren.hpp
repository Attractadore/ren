#pragma once
#include "ren.h"

#include <tl/expected.hpp>

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

enum class Error {
  Vulkan = REN_VULKAN_ERROR,
  System = REN_SYSTEM_ERROR,
  Runtime = REN_RUNTIME_ERROR,
  Unknown = REN_UNKNOWN_ERROR,
};

template <typename T> class expected : public tl::expected<T, Error> {
private:
  using Base = tl::expected<T, Error>;

public:
  using Base::Base;
  expected(Base &&e) : Base(std::move(e)) {}

  using Base::value;

  void value() const
    requires std::same_as<T, void>
  {
    if (not this->has_value()) {
      throw tl::bad_expected_access<Error>(this->error());
    }
  }
};

using unexpected = tl::unexpected<Error>;

namespace detail {
inline auto to_expected(RenResult result) -> expected<void> {
  if (result) {
    return unexpected(static_cast<Error>(result));
  }
  return {};
}
} // namespace detail

struct Scene;

namespace detail {

template <typename H>
concept CHandle = std::is_enum_v<H>;

template <typename P, CHandle H> constexpr void (P::*HandleDestroy)(H);

template <CHandle H> constexpr void *from_handle(H handle) {
  return reinterpret_cast<void *>(static_cast<uintptr_t>(handle));
}

template <CHandle H> constexpr H to_handle(void *handle) {
  return static_cast<H>(reinterpret_cast<uintptr_t>(handle));
}

template <typename P, CHandle H> struct HandleDeleter {
  P *parent = nullptr;

  void operator()(void *handle) {
    if (handle) {
      assert(parent);
      (parent->*HandleDestroy<P, H>)(to_handle<H>(handle));
    }
  }
};

template <typename P, CHandle H> class SharedHandle {
  std::shared_ptr<void> m_holder;

public:
  SharedHandle() = default;
  SharedHandle(P *parent, H handle)
      : m_holder(from_handle(handle), HandleDeleter<P, H>{parent}) {}

  auto get() const -> H { return to_handle<H>(m_holder.get()); }
  operator H() const { return get(); }

  explicit operator bool() const { return m_holder != nullptr; }

  auto get_parent() const -> const P * {
    return get_deleter<HandleDeleter<P, H>>(m_holder)->parent;
  }

  auto get_parent() -> P * {
    return get_deleter<HandleDeleter<P, H>>(m_holder)->parent;
  }
};

template <typename P, CHandle H> class UniqueHandle {
  friend class SharedHandle<P, H>;

  std::unique_ptr<void, HandleDeleter<P, H>> m_holder;

public:
  UniqueHandle() = default;
  UniqueHandle(P *parent, H handle)
      : m_holder(from_handle(handle), HandleDeleter<P, H>{parent}) {}

  operator SharedHandle<P, H>() && {
    return SharedHandle(get_parent(), to_handle<H>(m_holder.release()));
  }

  auto get() const -> H { return to_handle<H>(m_holder.get()); }
  operator H() const { return get(); }

  explicit operator bool() const { return m_holder != nullptr; }

  auto get_parent() const -> const P * { return m_holder.get_deleter().parent; }
  auto get_parent() -> P * { return m_holder.get_deleter().parent; }
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

namespace detail {
template <CHandle H> using UniqueSceneHandle = UniqueHandle<Scene, H>;
template <CHandle H> using SharedSceneHandle = SharedHandle<Scene, H>;
} // namespace detail

using MeshID = RenMesh;
constexpr MeshID NullMesh = REN_NULL_MESH;
using UniqueMeshID = detail::UniqueSceneHandle<MeshID>;
using SharedMeshID = detail::SharedSceneHandle<MeshID>;

using MaterialID = RenMaterial;
constexpr MaterialID NullMaterial = REN_NULL_MATERIAL;
using UniqueMaterialID = detail::UniqueSceneHandle<MaterialID>;
using SharedMaterialID = detail::SharedSceneHandle<MaterialID>;

using MeshInstanceID = RenMeshInstance;
constexpr MeshInstanceID NullMeshInstance = REN_NULL_MESH_INSTANCE;
using UniqueMeshInstanceID = detail::UniqueSceneHandle<MeshInstanceID>;
using SharedMeshInstanceID = detail::SharedSceneHandle<MeshInstanceID>;

using Vector3 = RenVector3;
using Matrix4x4 = RenMatrix4x4;

struct Device : RenDevice {
  [[nodiscard]] auto create_scene() -> expected<UniqueScene> {
    RenScene *scene;
    return detail::to_expected(ren_CreateScene(this, &scene)).map([&] {
      return UniqueScene(reinterpret_cast<Scene *>(scene));
    });
  }
};

struct Swapchain : RenSwapchain {
  void set_size(unsigned width, unsigned height) {
    ren_SetSwapchainSize(this, width, height);
  }

  auto get_size() const -> std::tuple<unsigned, unsigned> {
    unsigned width, height;
    ren_GetSwapchainSize(this, &width, &height);
    return {width, height};
  }
};

using PerspectiveProjection = RenPerspectiveProjection;
using OrthographicProjection = RenOrthographicProjection;
using CameraProjection =
    std::variant<PerspectiveProjection, OrthographicProjection>;

struct CameraDesc {
  CameraProjection projection;
  Vector3 position;
  Vector3 forward;
  Vector3 up;
};

struct MeshDesc {
  std::span<const Vector3> positions;
  std::span<const Vector3> colors;
  std::span<const unsigned> indices;
};

struct ConstMaterialAlbedo {
  Vector3 color;
};

struct VertexMaterialAlbedo {};

using MaterialAlbedo = std::variant<ConstMaterialAlbedo, VertexMaterialAlbedo>;

struct MaterialDesc {
  MaterialAlbedo albedo;
};

struct MeshInstanceDesc {
  MeshID mesh;
  MaterialID material;
};

struct Scene : RenScene {
  void set_camera(const CameraDesc &desc) {
    RenCameraDesc c_desc = {};
    std::visit(detail::overload_set{
                   [&](const PerspectiveProjection &perspective) {
                     c_desc.projection = REN_PROJECTION_PERSPECTIVE;
                     c_desc.perspective = perspective;
                   },
                   [&](const OrthographicProjection &ortho) {
                     c_desc.projection = REN_PROJECTION_ORTHOGRAPHIC;
                     c_desc.orthographic = ortho;
                   },
               },
               desc.projection);
    std::memcpy(c_desc.position, &desc.position, sizeof(desc.position));
    std::memcpy(c_desc.forward, &desc.forward, sizeof(desc.forward));
    std::memcpy(c_desc.up, &desc.up, sizeof(desc.up));
    ren_SetSceneCamera(this, &c_desc);
  }

  [[nodiscard]] auto create_mesh(const MeshDesc &desc) -> expected<MeshID> {
    assert(not desc.positions.empty());
    assert(desc.colors.empty() or desc.colors.size() == desc.positions.size());
    assert(not desc.indices.empty());
    RenMeshDesc c_desc = {
        .num_vertices = unsigned(desc.positions.size()),
        .num_indices = unsigned(desc.indices.size()),
        .positions = desc.positions.data(),
        .colors = desc.colors.empty() ? nullptr : desc.colors.data(),
        .indices = desc.indices.data(),
    };
    RenMesh mesh;
    return detail::to_expected(ren_CreateMesh(this, &c_desc, &mesh)).map([&] {
      return static_cast<MeshID>(mesh);
    });
  }

  void destroy_mesh(MeshID mesh) {
    ren_DestroyMesh(this, static_cast<RenMesh>(mesh));
  }

  [[nodiscard]] auto create_unique_mesh(const MeshDesc &desc)
      -> expected<UniqueMeshID> {
    return create_mesh(desc).map(
        [&](MeshID mesh) { return UniqueMeshID(this, mesh); });
  }

  [[nodiscard]] auto create_material(const MaterialDesc &desc)
      -> expected<MaterialID> {
    RenMaterialDesc c_desc = {};
    std::visit(
        detail::overload_set{[&](const ConstMaterialAlbedo &albedo) {
                               c_desc.albedo = REN_MATERIAL_ALBEDO_CONST;
                               std::memcpy(&c_desc.const_albedo, &albedo.color,
                                           sizeof(albedo.color));
                             },
                             [&](const VertexMaterialAlbedo &albedo) {
                               c_desc.albedo = REN_MATERIAL_ALBEDO_VERTEX;
                             }},
        desc.albedo);
    RenMaterial material;
    return detail::to_expected(ren_CreateMaterial(this, &c_desc, &material))
        .map([&] { return static_cast<MaterialID>(material); });
  }

  void destroy_material(MaterialID material) {
    ren_DestroyMaterial(this, static_cast<RenMaterial>(material));
  }

  [[nodiscard]] auto create_unique_material(const MaterialDesc &desc)
      -> expected<UniqueMaterialID> {
    return create_material(desc).map(
        [&](MaterialID material) { return UniqueMaterialID(this, material); });
  }

  [[nodiscard]] auto create_mesh_instance(const MeshInstanceDesc &desc)
      -> expected<MeshInstanceID> {
    RenMeshInstanceDesc c_desc = {
        .mesh = static_cast<RenMesh>(desc.mesh),
        .material = static_cast<RenMaterial>(desc.material),
    };
    RenMeshInstance model;
    return detail::to_expected(ren_CreateMeshInstance(this, &c_desc, &model))
        .map([&] { return static_cast<MeshInstanceID>(model); });
  }

  void destroy_mesh_instance(MeshInstanceID mesh_instance) {
    ren_DestroyMeshInstance(this, static_cast<RenMeshInstance>(mesh_instance));
  }

  [[nodiscard]] auto create_unique_mesh_instance(const MeshInstanceDesc &desc)
      -> expected<UniqueMeshInstanceID> {
    return create_mesh_instance(desc).map([&](MeshInstanceID mesh_instance) {
      return UniqueMeshInstanceID(this, mesh_instance);
    });
  }

  void set_model_matrix(MeshInstanceID model, const Matrix4x4 &matrix) {
    ren_SetMeshInstanceMatrix(this, static_cast<RenMeshInstance>(model),
                              &matrix);
  }

  [[nodiscard]] auto draw(Swapchain &swapchain, unsigned width, unsigned height)
      -> expected<void> {
    return detail::to_expected(ren_SceneDraw(this, &swapchain, width, height));
  }
};

namespace detail {

template <>
inline constexpr void (Scene::*HandleDestroy<Scene, MeshID>)(MeshID) =
    &Scene::destroy_mesh;
template <>
inline constexpr void (Scene::*HandleDestroy<Scene, MaterialID>)(MaterialID) =
    &Scene::destroy_material;
template <>
inline constexpr void (Scene::*HandleDestroy<Scene, MeshInstanceID>)(
    MeshInstanceID) = &Scene::destroy_mesh_instance;

} // namespace detail

} // namespace v0
} // namespace ren
