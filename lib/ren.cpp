#include "ren/ren.h"
#include "Device.hpp"
#include "Scene.hpp"
#include "Support/Errors.hpp"
#include "Swapchain.hpp"
#include "ren/ren-vk.h"

#include <range/v3/algorithm.hpp>
#include <range/v3/view.hpp>

#include <cassert>

namespace {

template <std::invocable F>
  requires std::same_as<std::invoke_result_t<F>, void>
RenResult lippincott(F f) noexcept {
  try {
    f();
    return REN_SUCCESS;
  } catch (const std::system_error &) {
    return REN_SYSTEM_ERROR;
  } catch (const std::runtime_error &e) {
    if (std::string_view(e.what()).starts_with("Vulkan")) {
      return REN_VULKAN_ERROR;
    }
    return REN_RUNTIME_ERROR;
  } catch (...) {
    return REN_UNKNOWN_ERROR;
  }
}

} // namespace

extern "C" {

static_assert(sizeof(RenVector2) == sizeof(glm::vec2));
static_assert(sizeof(RenVector3) == sizeof(glm::vec3));
static_assert(sizeof(RenVector4) == sizeof(glm::vec4));
static_assert(sizeof(RenMatrix4x4) == sizeof(glm::mat4));

uint32_t ren_vk_GetRequiredAPIVersion() {
  return ren::Device::getRequiredAPIVersion();
}

size_t ren_vk_GetRequiredLayerCount() {
  return ren::Device::getRequiredLayers().size();
}

const char *const *ren_vk_GetRequiredLayers() {
  return ren::Device::getRequiredLayers().data();
}

size_t ren_vk_GetRequiredExtensionCount() {
  return ren::Device::getInstanceExtensions().size();
}

const char *const *ren_vk_GetRequiredExtensions() {
  return ren::Device::getInstanceExtensions().data();
}

namespace ren {
namespace {

#define load_vulkan_function(proc, instance, name)                             \
  [&]() {                                                                      \
    auto *func = reinterpret_cast<PFN_##name>(proc(instance, #name));          \
    assert(func);                                                              \
    return func;                                                               \
  }()

VkInstance create_instance(PFN_vkGetInstanceProcAddr proc,
                           std::span<const char *const> external_extensions) {
  VkApplicationInfo application_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .apiVersion = Device::getRequiredAPIVersion(),
  };

  auto layers = Device::getRequiredLayers();

  SmallVector<const char *> extensions(external_extensions);
  extensions.append(Device::getInstanceExtensions());

  VkInstanceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &application_info,
      .enabledLayerCount = static_cast<uint32_t>(layers.size()),
      .ppEnabledLayerNames = layers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };

  auto vk_create_instance =
      load_vulkan_function(proc, VK_NULL_HANDLE, vkCreateInstance);

  VkInstance instance;
  throw_if_failed(vk_create_instance(&create_info, nullptr, &instance),
                  "Vulkan: Failed to create VkInstance");

  return instance;
}

VkPhysicalDevice find_adapter(PFN_vkGetInstanceProcAddr proc,
                              VkInstance instance,
                              std::span<const uint8_t, VK_UUID_SIZE> uuid) {
  auto vk_enumerate_physical_devices =
      load_vulkan_function(proc, instance, vkEnumeratePhysicalDevices);
  auto vk_get_physical_device_properties =
      load_vulkan_function(proc, instance, vkGetPhysicalDeviceProperties);

  unsigned num_devices = 0;
  throw_if_failed(
      vk_enumerate_physical_devices(instance, &num_devices, nullptr),
      "Vulkan: Failed to enumerate physical device");
  SmallVector<VkPhysicalDevice> devices(num_devices);
  throw_if_failed(
      vk_enumerate_physical_devices(instance, &num_devices, devices.data()),
      "Vulkan: Failed to enumerate physical device");
  devices.resize(num_devices);

  for (auto device : devices) {
    VkPhysicalDeviceProperties props;
    vk_get_physical_device_properties(device, &props);
    if (ranges::equal(uuid, std::span(props.pipelineCacheUUID))) {
      return device;
    }
  }

  return VK_NULL_HANDLE;
}

} // namespace
} // namespace ren

RenResult ren_vk_CreateDevice(const RenDeviceDesc *desc, RenDevice **p_device) {
  assert(desc);
  assert(desc->proc);
  if (desc->num_instance_extensions > 0) {
    assert(desc->instance_extensions);
  }
  assert(p_device);

  RenResult result = REN_SUCCESS;
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice adapter = VK_NULL_HANDLE;

  result = lippincott([&] {
    instance = ren::create_instance(
        desc->proc, {desc->instance_extensions, desc->num_instance_extensions});
  });
  if (result) {
    goto clean;
  }

  result = lippincott([&] {
    adapter =
        ren::find_adapter(desc->proc, instance, desc->pipeline_cache_uuid);
    ren::throw_if_failed(adapter, "Vulkan: Failed to find physical device");
  });
  if (result) {
    goto clean;
  }

  result = lippincott(
      [&] { *p_device = new RenDevice(desc->proc, instance, adapter); });
  if (result) {
    goto clean;
  }

  return REN_SUCCESS;

clean:
  if (instance) {
    auto *vk_destroy_instance =
        load_vulkan_function(desc->proc, instance, vkDestroyInstance);
    vk_destroy_instance(instance, nullptr);
  }

  return result;
}

#undef load_vulkan_function

void ren_DestroyDevice(RenDevice *device) { delete device; }

RenResult ren_vk_CreateSwapchain(RenDevice *device,
                                 RenPFNCreateSurface create_surface,
                                 void *usrptr, RenSwapchain **p_swapchain) {
  assert(device);
  assert(create_surface);
  assert(p_swapchain);

  RenResult result = REN_SUCCESS;
  VkSurfaceKHR surface = VK_NULL_HANDLE;

  result = lippincott([&] {
    ren::throw_if_failed(
        create_surface(device->getInstance(), usrptr, &surface),
        "Vulkan: Failed to create surface");
  });
  if (result) {
    goto clean;
  }

  result =
      lippincott([&] { *p_swapchain = new RenSwapchain(*device, surface); });
  if (result) {
    goto clean;
  }

  return REN_SUCCESS;

clean:
  if (surface) {
    device->DestroySurfaceKHR(surface);
  }

  return result;
}

void ren_DestroySwapchain(RenSwapchain *swapchain) { delete swapchain; }

void ren_SetSwapchainSize(RenSwapchain *swapchain, unsigned width,
                          unsigned height) {
  assert(swapchain);
  assert(width);
  assert(height);
  swapchain->set_size(width, height);
}

void ren_GetSwapchainSize(const RenSwapchain *swapchain, unsigned *p_width,
                          unsigned *p_height) {
  assert(swapchain);
  assert(p_width);
  assert(p_height);
  auto [width, height] = swapchain->get_size();
  *p_width = width;
  *p_height = height;
}

VkSurfaceKHR ren_vk_GetSwapchainSurface(const RenSwapchain *swapchain) {
  assert(swapchain);
  return swapchain->get_surface();
}

VkPresentModeKHR ren_vk_GetSwapchainPresentMode(const RenSwapchain *swapchain) {
  assert(swapchain);
  return swapchain->get_present_mode();
}

RenResult ren_vk_SetSwapchainPresentMode(RenSwapchain *swapchain,
                                         VkPresentModeKHR present_mode) {
  assert(swapchain);
  return lippincott([&] { swapchain->set_present_mode(present_mode); });
}

RenResult ren_CreateScene(RenDevice *device, RenSwapchain *swapchain,
                          RenScene **p_scene) {
  assert(device);
  assert(swapchain);
  assert(p_scene);
  return lippincott([&] { *p_scene = new RenScene(*device, *swapchain); });
}

void ren_DestroyScene(RenScene *scene) { delete scene; }

RenResult ren_DrawScene(RenScene *scene) {
  assert(scene);
  return lippincott([&] { scene->draw(); });
}

RenResult ren_SetSceneCamera(RenScene *scene, const RenCameraDesc *desc) {
  assert(scene);
  assert(desc);
  return lippincott([&] { scene->set_camera(*desc); });
}

RenResult ren_SetSceneToneMapping(RenScene *scene,
                                  RenToneMappingOperator oper) {
  assert(scene);
  return lippincott([&] { scene->set_tone_mapping(oper); });
}

RenResult ren_CreateMesh(RenScene *scene, const RenMeshDesc *desc,
                         RenMesh *p_mesh) {
  assert(scene);
  assert(desc);
  assert(p_mesh);
  return lippincott([&] { *p_mesh = scene->create_mesh(*desc); });
}

RenResult ren_CreateImage(RenScene *scene, const RenImageDesc *desc,
                          RenImage *p_image) {
  assert(scene);
  assert(desc);
  assert(p_image);
  return lippincott([&] { *p_image = scene->create_image(*desc); });
}

RenResult ren_CreateMaterials(RenScene *scene, const RenMaterialDesc *descs,
                              size_t count, RenMaterial *materials) {
  assert(scene);
  assert(descs);
  assert(materials);
  return lippincott(
      [&] { scene->create_materials(std::span(descs, count), materials); });
}

RenResult ren_CreateMeshInsts(RenScene *scene, const RenMeshInstDesc *descs,
                              size_t count, RenMeshInst *mesh_insts) {
  assert(scene);
  assert(descs);
  assert(mesh_insts);
  return lippincott(
      [&] { scene->create_mesh_insts(std::span(descs, count), mesh_insts); });
}

void ren_DestroyMeshInsts(RenScene *scene, const RenMeshInst *mesh_insts,
                          size_t count) {
  assert(scene);
  assert(mesh_insts);
  scene->destroy_mesh_insts(std::span(mesh_insts, count));
}

void ren_SetMeshInstMatrices(RenScene *scene, const RenMeshInst *mesh_insts,
                             const RenMatrix4x4 *matrices, size_t count) {
  assert(scene);
  assert(mesh_insts);
  assert(matrices);
  scene->set_mesh_inst_matrices(std::span(mesh_insts, count),
                                std::span(matrices, count));
}

RenResult ren_CreateDirLights(RenScene *scene, const RenDirLightDesc *descs,
                              size_t count, RenDirLight *lights) {
  assert(scene);
  assert(descs);
  assert(lights);
  return lippincott(
      [&] { scene->create_dir_lights(std::span(descs, count), lights); });
}

void ren_DestroyDirLights(RenScene *scene, const RenDirLight *lights,
                          size_t count) {
  assert(scene);
  assert(lights);
  scene->destroy_dir_lights(std::span(lights, count));
}

RenResult ren_ConfigDirLights(RenScene *scene, const RenDirLight *lights,
                              const RenDirLightDesc *descs, size_t count) {
  assert(scene);
  assert(lights);
  assert(descs);
  return lippincott([&] {
    scene->config_dir_lights(std::span(lights, count), std::span(descs, count));
  });
}

RenResult ren_CreatePointLights(RenScene *scene, const RenPointLightDesc *descs,
                                size_t count, RenPointLight *lights) {
  assert(scene);
  assert(descs);
  assert(lights);
  return lippincott([&] { ren::todo("Point light are not implemented!"); });
}

void ren_DestroyPointLights(RenScene *scene, const RenPointLight *lights,
                            size_t count) {
  assert(scene);
  assert(lights);
  ren::todo("Point light are not implemented!");
}

RenResult ren_ConfigPointLights(RenScene *scene, const RenPointLight *lights,
                                const RenPointLightDesc *descs, size_t count) {
  assert(scene);
  assert(lights);
  assert(descs);
  return lippincott([&] { ren::todo("Point light are not implemented!"); });
}

RenResult ren_CreateSpotLights(RenScene *scene, const RenSpotLightDesc *descs,
                               size_t count, RenSpotLight *lights) {
  assert(scene);
  assert(descs);
  assert(lights);
  return lippincott([&] { ren::todo("Spot light are not implemented!"); });
}

void ren_DestroySpotLights(RenScene *scene, const RenSpotLight *lights,
                           size_t count) {
  assert(scene);
  assert(lights);
  ren::todo("Spot light are not implemented!");
}

RenResult ren_ConfigSpotLights(RenScene *scene, const RenSpotLight *lights,
                               const RenSpotLightDesc *descs, size_t count) {
  assert(scene);
  assert(lights);
  assert(descs);
  return lippincott([&] { ren::todo("Spot light are not implemented!"); });
}
}
