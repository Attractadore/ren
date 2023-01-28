#pragma once
#include "ren.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
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

struct Scene;

namespace detail {

template <typename H>
concept CScopedEnum = std::is_enum_v<H> and not
std::convertible_to<H, std::underlying_type_t<H>>;

template <typename H>
concept CHandle = CScopedEnum<H>;

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

  auto get_parent() const -> const P * { return m_holder.get_deleter().parent; }
  auto get_parent() -> P * { return m_holder.get_deleter().parent; }
};

template <class... Ts> struct overload_set : Ts... {
  overload_set(Ts... fs) : Ts(std::move(fs))... {}
  using Ts::operator()...;
};

template <typename T> class Frame {
protected:
  struct Deleter {
    void operator()(T *handle) noexcept(false) {
      if (handle) {
        if (std::uncaught_exceptions() == 0) {
          handle->end_frame().value();
        }
      }
    }
  };

private:
  std::unique_ptr<T, Deleter> m_holder;

protected:
  Frame(std::unique_ptr<T, Deleter> handle) : m_holder(std::move(handle)) {}

public:
  template <typename... Args>
  Frame(T &handle, Args &&...args)
      : Frame(begin(handle, std::forward<Args>(args)...).value()) {}
  Frame(Frame &&) = default;
  Frame &operator=(Frame &&) = default;
  ~Frame() noexcept(false) = default;

  template <typename... Args>
  [[nodiscard]] auto begin(T &handle, Args &&...args) -> expected<Frame> {
    return handle.begin_frame(std::forward<Args>(args)...).map([&] {
      return Frame(std::unique_ptr<T, Deleter>(&handle));
    });
  }

  auto get() const -> const T & { return *m_holder.get(); }
  auto get() -> T & { return *m_holder.get(); }

  auto operator->() const -> const T * { return m_holder.get(); }
  auto operator->() -> T * { return m_holder.get(); }
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

enum class MeshID : RenMesh;
constexpr auto NullMesh = static_cast<MeshID>(0);
using UniqueMeshID = detail::UniqueSceneHandle<MeshID>;
using SharedMeshID = detail::SharedSceneHandle<MeshID>;

enum class MaterialID : RenMaterial;
constexpr auto NullMaterial = static_cast<MaterialID>(0);
using UniqueMaterialID = detail::UniqueSceneHandle<MaterialID>;
using SharedMaterialID = detail::SharedSceneHandle<MaterialID>;

enum class ModelID : RenModel;
constexpr auto NullModel = static_cast<ModelID>(0);
using UniqueModelID = detail::UniqueSceneHandle<ModelID>;
using SharedModelID = detail::SharedSceneHandle<ModelID>;

struct Device : RenDevice {
  using Frame = detail::Frame<Device>;

  [[nodiscard]] auto begin_frame() -> expected<void> {
    if (auto err = ren_DeviceBeginFrame(this)) {
      return unexpected(static_cast<Error>(err));
    }
    return {};
  }

  [[nodiscard]] auto end_frame() -> expected<void> {
    if (auto err = ren_DeviceEndFrame(this)) {
      return unexpected(static_cast<Error>(err));
    }
    return {};
  }

  [[nodiscard]] auto create_scene() -> expected<UniqueScene> {
    RenScene *scene = nullptr;
    if (auto err = ren_CreateScene(this, &scene)) {
      return unexpected(static_cast<Error>(err));
    }
    return UniqueScene(reinterpret_cast<Scene *>(scene), SceneDeleter());
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

struct CameraDesc {
  std::variant<PerspectiveProjection, OrthographicProjection> projection;
  glm::vec3 position;
  glm::vec3 forward;
  glm::vec3 up;
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
  MeshID mesh;
  MaterialID material;
};

struct Scene : RenScene {
  using Frame = detail::Frame<Scene>;

  [[nodiscard]] auto begin_frame(Swapchain &swapchain) -> expected<void> {
    if (auto err = ren_SceneBeginFrame(this, &swapchain)) {
      return unexpected(static_cast<Error>(err));
    }
    return {};
  }

  [[nodiscard]] auto end_frame() -> expected<void> {
    if (auto err = ren_SceneEndFrame(this)) {
      return unexpected(static_cast<Error>(err));
    }
    return {};
  }

  [[nodiscard]] auto set_output_size(unsigned width, unsigned height)
      -> expected<void> {
    if (auto err = ren_SetSceneOutputSize(this, width, height)) {
      return unexpected(static_cast<Error>(err));
    }
    return {};
  }

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
        .positions = reinterpret_cast<const float *>(desc.positions.data()),
        .colors = desc.colors.empty()
                      ? nullptr
                      : reinterpret_cast<const float *>(desc.colors.data()),
        .indices = desc.indices.data(),
    };
    RenMesh mesh = 0;
    if (auto err = ren_CreateMesh(this, &c_desc, &mesh)) {
      return unexpected(static_cast<Error>(err));
    }
    return static_cast<MeshID>(mesh);
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
    RenMaterial material = 0;
    if (auto err = ren_CreateMaterial(this, &c_desc, &material)) {
      return unexpected(static_cast<Error>(err));
    }
    return static_cast<MaterialID>(material);
  }

  void destroy_material(MaterialID material) {
    ren_DestroyMaterial(this, static_cast<RenMaterial>(material));
  }

  [[nodiscard]] auto create_unique_material(const MaterialDesc &desc)
      -> expected<UniqueMaterialID> {
    return create_material(desc).map(
        [&](MaterialID material) { return UniqueMaterialID(this, material); });
  }

  [[nodiscard]] auto create_model(const ModelDesc &desc) -> expected<ModelID> {
    RenModelDesc c_desc = {
        .mesh = static_cast<RenMesh>(desc.mesh),
        .material = static_cast<RenMaterial>(desc.material),
    };
    RenModel model = 0;
    if (auto err = ren_CreateModel(this, &c_desc, &model)) {
      return unexpected(static_cast<Error>(err));
    }
    return static_cast<ModelID>(model);
  }

  void destroy_model(ModelID model) {
    ren_DestroyModel(this, static_cast<RenModel>(model));
  }

  [[nodiscard]] auto create_unique_model(const ModelDesc &desc)
      -> expected<UniqueModelID> {
    return create_model(desc).map(
        [&](ModelID model) { return UniqueModelID(this, model); });
  }

  void set_model_matrix(ModelID model, const glm::mat4 &matrix) {
    ren_SetModelMatrix(this, static_cast<RenModel>(model),
                       glm::value_ptr(matrix));
  }
};

class Mesh {
  SharedMeshID m_mesh;

public:
  Mesh() = default;

  explicit Mesh(SharedMeshID mesh) noexcept : m_mesh(std::move(mesh)) {}

  Mesh(Scene::Frame &scene, const MeshDesc &desc)
      : Mesh(create(scene, desc).value()) {}

  [[nodiscard]] static auto create(Scene::Frame &scene, const MeshDesc &desc)
      -> expected<Mesh> {
    return scene->create_unique_mesh(desc).map(
        [](SharedMeshID mesh) { return Mesh(std::move(mesh)); });
  }

  auto get() const & -> const SharedMeshID & { return m_mesh; }
  auto get() && -> SharedMeshID && { return std::move(m_mesh); }

  explicit operator bool() const { return get() != NullMesh; }
};

class Material {
  SharedMaterialID m_material;

public:
  Material() = default;

  explicit Material(SharedMaterialID material) noexcept
      : m_material(std::move(material)) {}

  Material(Scene::Frame &scene, const MaterialDesc &desc)
      : Material(create(scene, desc).value()) {}

  [[nodiscard]] static auto create(Scene::Frame &scene,
                                   const MaterialDesc &desc)
      -> expected<Material> {
    return scene->create_unique_material(desc).map(
        [](SharedMaterialID material) {
          return Material(std::move(material));
        });
  }

  auto get() const & -> const SharedMaterialID & { return m_material; }
  auto get() && -> SharedMaterialID && { return std::move(m_material); }

  explicit operator bool() const { return get() != NullMaterial; }
};

namespace detail {

template <typename Model> class ModelMixin {
  auto impl() const -> const Model & {
    return static_cast<const Model &>(*this);
  }

  auto impl() -> Model & { return static_cast<Model &>(*this); }

  auto get() const -> ModelID { return impl().get(); }

  auto get_scene() const -> const Scene * { return impl().get_scene(); }
  auto get_scene() -> Scene * { return impl().get_scene(); }

public:
  explicit operator bool() const { return get() != NullModel; }

  void set_matrix(const glm::mat4 &matrix) {
    get_scene()->set_model_matrix(get(), matrix);
  }
};

} // namespace detail

class SharedModel : public detail::ModelMixin<SharedModel> {
  SharedModelID m_model;
  SharedMeshID m_mesh;
  SharedMaterialID m_material;

public:
  SharedModel() = default;

  SharedModel(SharedModelID model, SharedMeshID mesh,
              SharedMaterialID material) noexcept
      : m_model(std::move(model)), m_mesh(std::move(mesh)),
        m_material(std::move(material)) {}

  auto get() const & -> const SharedModelID & { return m_model; }
  auto get() && -> SharedModelID && { return std::move(m_model); }

  auto get_scene() const -> const Scene * { return m_model.get_parent(); }
  auto get_scene() -> Scene * { return m_model.get_parent(); }
};

class Model : public detail::ModelMixin<Model> {
  UniqueModelID m_model;
  SharedMeshID m_mesh;
  SharedMaterialID m_material;

public:
  Model() = default;
  Model(Scene::Frame &scene, Mesh mesh, Material material)
      : Model(create(scene, std::move(mesh), std::move(material)).value()) {}

  Model(UniqueModelID model, SharedMeshID mesh,
        SharedMaterialID material) noexcept
      : m_model(std::move(model)), m_mesh(std::move(mesh)),
        m_material(std::move(material)) {}

  [[nodiscard]] static auto create(Scene::Frame &scene, Mesh mesh,
                                   Material material) -> expected<Model> {
    return scene
        ->create_unique_model({
            .mesh = mesh.get(),
            .material = material.get(),
        })
        .map([&](UniqueModelID model) {
          return Model(std::move(model), std::move(mesh).get(),
                       std::move(material).get());
        });
  }

  operator SharedModel() && {
    return SharedModel(std::move(m_model), std::move(m_mesh),
                       std::move(m_material));
  }

  auto get() const & -> const UniqueModelID & { return m_model; }
  auto get() && -> UniqueModelID && { return std::move(m_model); }

  auto get_scene() const -> const Scene * { return m_model.get_parent(); }
  auto get_scene() -> Scene * { return m_model.get_parent(); }
};

namespace detail {

template <>
inline constexpr void (Scene::*HandleDestroy<Scene, MeshID>)(MeshID) =
    &Scene::destroy_mesh;
template <>
inline constexpr void (Scene::*HandleDestroy<Scene, MaterialID>)(MaterialID) =
    &Scene::destroy_material;
template <>
inline constexpr void (Scene::*HandleDestroy<Scene, ModelID>)(ModelID) =
    &Scene::destroy_model;

} // namespace detail

} // namespace v0
} // namespace ren
