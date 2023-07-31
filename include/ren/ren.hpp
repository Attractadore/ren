#pragma once
#include "ren.h"

#include <cassert>
#include <cstring>
#include <expected>
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

using unexpected = std::unexpected<Error>;

template <typename T> using expected = std::expected<T, Error>;

namespace detail {

inline auto make_expected(RenResult result) -> expected<void> {
  if (result) {
    return unexpected(static_cast<Error>(result));
  }
  return {};
}

} // namespace detail

using Vector2 = RenVector2;
using Vector3 = RenVector3;
using Vector4 = RenVector4;
using Matrix4x4 = RenMatrix4x4;

using MeshID = RenMesh;
constexpr MeshID NullMesh = REN_NULL_MESH;

using ImageID = RenImage;
constexpr ImageID NullImage = REN_NULL_IMAGE;

using MaterialID = RenMaterial;
constexpr MaterialID NullMaterial = REN_NULL_MATERIAL;

using MeshInstID = RenMeshInst;
constexpr MeshInstID NullMeshInst = REN_NULL_MESH_INST;

using DirLightID = RenDirLight;
constexpr DirLightID NullDirLight = REN_NULL_DIR_LIGHT;

using PointLightID = RenPointLight;
constexpr PointLightID NullPointLight = REN_NULL_POINT_LIGHT;

using SpotLightID = RenSpotLight;
constexpr SpotLightID NullSpotLight = REN_NULL_SPOT_LIGHT;

using PerspectiveProjection = RenPerspectiveProjection;
using OrthographicProjection = RenOrthographicProjection;
using CameraDesc = RenCameraDesc;

using ToneMappingOperator = RenToneMappingOperator;

using MeshDesc = RenMeshDesc;

using Format = RenFormat;
using ImageDesc = RenImageDesc;

using Filter = RenFilter;
using WrappingMode = RenWrappingMode;
using Sampler = RenSampler;
using TextureChannel = RenTextureChannel;
using TextureChannelSwizzle = RenTextureChannelSwizzle;
using Texture = RenTexture;

using AlphaMode = RenAlphaMode;
using MaterialDesc = RenMaterialDesc;

using MeshInstDesc = RenMeshInstDesc;

using DirLightDesc = RenDirLightDesc;

using PointLightDesc = RenPointLightDesc;

using SpotLightDesc = RenSpotLightDesc;

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

struct Scene;
struct SceneDeleter {
  void operator()(Scene *scene) const noexcept {
    ren_DestroyScene(reinterpret_cast<RenScene *>(scene));
  }
};
using UniqueScene = std::unique_ptr<Scene, SceneDeleter>;
using SharedScene = std::shared_ptr<Scene>;

struct Device : RenDevice {};

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

struct Scene : RenScene {
  [[nodiscard]] static auto create(Device &device, Swapchain &swapchain)
      -> expected<UniqueScene> {
    RenScene *scene;
    return detail::make_expected(ren_CreateScene(&device, &swapchain, &scene))
        .transform(
            [&] { return UniqueScene(reinterpret_cast<Scene *>(scene)); });
  }

  [[nodiscard]] auto set_camera(const CameraDesc &desc) -> expected<void> {
    return detail::make_expected(ren_SetSceneCamera(this, &desc));
  }

  [[nodiscard]] auto set_tone_mapping(ToneMappingOperator oper)
      -> expected<void> {
    return detail::make_expected(ren_SetSceneToneMapping(this, oper));
  }

  [[nodiscard]] auto draw() -> expected<void> {
    return detail::make_expected(ren_DrawScene(this));
  }

  [[nodiscard]] auto create_mesh(const MeshDesc &desc) -> expected<MeshID> {
    RenMesh mesh;
    return detail::make_expected(ren_CreateMesh(this, &desc, &mesh))
        .transform([&] { return mesh; });
  }

  [[nodiscard]] auto create_image(const ImageDesc &desc) -> expected<ImageID> {
    RenImage image;
    return detail::make_expected(ren_CreateImage(this, &desc, &image))
        .transform([&] { return image; });
  }

  [[nodiscard]] auto create_material(std::span<const MaterialDesc> descs,
                                     std::span<MaterialID> materials)
      -> expected<std::span<MaterialID>> {
    assert(materials.size() >= descs.size());
    RenMaterial material;
    return detail::make_expected(ren_CreateMaterials(this, descs.data(),
                                                     descs.size(),
                                                     materials.data()))
        .transform([&] { return materials.first(descs.size()); });
  }

  [[nodiscard]] auto create_material(const MaterialDesc &desc)
      -> expected<MaterialID> {
    RenMaterial material;
    return detail::make_expected(ren_CreateMaterial(this, &desc, &material))
        .transform([&] { return material; });
  }

  [[nodiscard]] auto create_mesh_insts(std::span<const MeshInstDesc> descs,
                                       std::span<MeshInstID> mesh_insts)
      -> expected<std::span<MeshInstID>> {
    RenMeshInst model;
    return detail::make_expected(ren_CreateMeshInsts(this, descs.data(),
                                                     descs.size(),
                                                     mesh_insts.data()))
        .transform([&] { return mesh_insts.first(descs.size()); });
  }

  [[nodiscard]] auto create_mesh_inst(const MeshInstDesc &desc)
      -> expected<MeshInstID> {
    RenMeshInst model;
    return detail::make_expected(ren_CreateMeshInst(this, &desc, &model))
        .transform([&] { return model; });
  }

  void destroy_mesh_insts(std::span<const MeshInstID> mesh_insts) {
    ren_DestroyMeshInsts(this, mesh_insts.data(), mesh_insts.size());
  }

  void destroy_mesh_inst(MeshInstID mesh_inst) {
    ren_DestroyMeshInst(this, mesh_inst);
  }

  void set_mesh_inst_matrices(std::span<const MeshInstID> mesh_insts,
                              std::span<const Matrix4x4> matrices) {
    assert(matrices.size() >= mesh_insts.size());
    ren_SetMeshInstMatrices(this, mesh_insts.data(), matrices.data(),
                            mesh_insts.size());
  }

  void set_mesh_inst_matrix(MeshInstID mesh_inst, const Matrix4x4 &matrix) {
    ren_SetMeshInstMatrix(this, mesh_inst, &matrix);
  }

  [[nodiscard]] auto create_dir_lights(std::span<const DirLightDesc> descs,
                                       std::span<DirLightID> lights)
      -> expected<std::span<DirLightID>> {
    assert(lights.size() >= descs.size());
    return detail::make_expected(ren_CreateDirLights(this, descs.data(),
                                                     descs.size(),
                                                     lights.data()))
        .transform([&] { return lights.first(descs.size()); });
  }

  [[nodiscard]] auto create_dir_light(const DirLightDesc &desc)
      -> expected<DirLightID> {
    RenDirLight light;
    return detail::make_expected(ren_CreateDirLight(this, &desc, &light))
        .transform([&] { return light; });
  }

  void destroy_dir_lights(std::span<const DirLightID> lights) {
    ren_DestroyDirLights(this, lights.data(), lights.size());
  }

  void destroy_dir_light(DirLightID light) { ren_DestroyDirLight(this, light); }

  [[nodiscard]] auto config_dir_lights(std::span<const DirLightID> lights,
                                       std::span<const DirLightDesc> descs)
      -> expected<void> {
    assert(lights.size() <= descs.size());
    return detail::make_expected(
        ren_ConfigDirLights(this, lights.data(), descs.data(), lights.size()));
  }

  [[nodiscard]] auto config_dir_light(DirLightID light,
                                      const DirLightDesc &desc)
      -> expected<void> {
    return detail::make_expected(ren_ConfigDirLight(this, light, &desc));
  }

  [[nodiscard]] auto create_point_lights(std::span<const PointLightDesc> descs,
                                         std::span<PointLightID> lights)
      -> expected<std::span<PointLightID>> {
    assert(lights.size() >= descs.size());
    return detail::make_expected(ren_CreatePointLights(this, descs.data(),
                                                       descs.size(),
                                                       lights.data()))
        .transform([&] { return lights.first(descs.size()); });
  }

  [[nodiscard]] auto create_point_light(const PointLightDesc &desc)
      -> expected<PointLightID> {
    RenPointLight light;
    return detail::make_expected(ren_CreatePointLight(this, &desc, &light))
        .transform([&] { return light; });
  }

  void destroy_point_lights(std::span<const PointLightID> lights) {
    ren_DestroyPointLights(this, lights.data(), lights.size());
  }

  void destroy_point_light(PointLightID light) {
    ren_DestroyPointLight(this, light);
  }

  [[nodiscard]] auto config_point_lights(std::span<const PointLightID> lights,
                                         std::span<const PointLightDesc> descs)
      -> expected<void> {
    assert(lights.size() <= descs.size());
    return detail::make_expected(ren_ConfigPointLights(
        this, lights.data(), descs.data(), lights.size()));
  }

  [[nodiscard]] auto config_point_light(PointLightID light,
                                        const PointLightDesc &desc)
      -> expected<void> {
    return detail::make_expected(ren_ConfigPointLight(this, light, &desc));
  }

  [[nodiscard]] auto create_spot_lights(std::span<const SpotLightDesc> descs,
                                        std::span<SpotLightID> lights)
      -> expected<std::span<SpotLightID>> {
    assert(lights.size() >= descs.size());
    return detail::make_expected(ren_CreateSpotLights(this, descs.data(),
                                                      descs.size(),
                                                      lights.data()))
        .transform([&] { return lights.first(descs.size()); });
  }

  [[nodiscard]] auto create_spot_light(const SpotLightDesc &desc)
      -> expected<SpotLightID> {
    RenSpotLight light;
    return detail::make_expected(ren_CreateSpotLight(this, &desc, &light))
        .transform([&] { return light; });
  }

  void destroy_spot_lights(std::span<const SpotLightID> lights) {
    ren_DestroySpotLights(this, lights.data(), lights.size());
  }

  void destroy_spot_light(SpotLightID light) {
    ren_DestroySpotLight(this, light);
  }

  [[nodiscard]] auto config_spot_lights(std::span<const SpotLightID> lights,
                                        std::span<const SpotLightDesc> descs)
      -> expected<void> {
    assert(lights.size() <= descs.size());
    return detail::make_expected(
        ren_ConfigSpotLights(this, lights.data(), descs.data(), lights.size()));
  }

  [[nodiscard]] auto config_spot_light(SpotLightID light,
                                       const SpotLightDesc &desc)
      -> expected<void> {
    return detail::make_expected(ren_ConfigSpotLight(this, light, &desc));
  }
};

} // namespace v0
} // namespace ren
