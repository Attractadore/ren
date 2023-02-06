#include "ren/ren.h"
#include "Device.hpp"
#include "Scene.hpp"
#include "Swapchain.hpp"
#include "ren/ren-vk.h"

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

static_assert(sizeof(RenVector3) == sizeof(glm::vec3));
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
  return ren::Device::getRequiredExtensions().size();
}

const char *const *ren_vk_GetRequiredExtensions() {
  return ren::Device::getRequiredExtensions().data();
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
  extensions.append(Device::getRequiredExtensions());

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
  throwIfFailed(vk_create_instance(&create_info, nullptr, &instance),
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
  throwIfFailed(vk_enumerate_physical_devices(instance, &num_devices, nullptr),
                "Vulkan: Failed to enumerate physical device");
  SmallVector<VkPhysicalDevice> devices(num_devices);
  throwIfFailed(
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
    ren::throwIfFailed(adapter, "Vulkan: Failed to find physical device");
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
    ren::throwIfFailed(create_surface(device->getInstance(), usrptr, &surface),
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

RenResult ren_CreateScene(RenDevice *device, RenScene **p_scene) {
  assert(device);
  assert(p_scene);
  return lippincott([&] { *p_scene = new RenScene(*device); });
}

void ren_DestroyScene(RenScene *scene) { delete scene; }

RenResult ren_SetViewport(RenScene *scene, unsigned width, unsigned height) {
  assert(scene);
  assert(width > 0);
  assert(height > 0);
  return lippincott([&] {
    scene->m_viewport_width = width;
    scene->m_viewport_height = height;
  });
}

RenResult ren_DrawScene(RenScene *scene, RenSwapchain *swapchain) {
  assert(scene);
  assert(swapchain);
  return lippincott([&] { scene->draw(*swapchain); });
}

RenResult ren_CreateMesh(RenScene *scene, const RenMeshDesc *desc,
                         RenMesh *p_mesh) {
  assert(scene);
  assert(desc);
  assert(desc->num_vertices > 0);
  assert(desc->positions);
  assert(desc->normals);
  assert(desc->num_indices > 0);
  assert(desc->indices);
  assert(p_mesh);
  return lippincott([&] { *p_mesh = scene->create_mesh(*desc); });
}

void ren_DestroyMesh(RenScene *scene, RenMesh mesh) {
  assert(scene);
  scene->destroy_mesh(mesh);
}

RenResult ren_CreateMaterial(RenScene *scene, const RenMaterialDesc *desc,
                             RenMaterial *p_material) {
  assert(scene);
  assert(desc);
  return lippincott([&] { *p_material = scene->create_material(*desc); });
}

void ren_DestroyMaterial(RenScene *scene, RenMaterial material) {
  assert(scene);
  scene->destroy_material(material);
}

void ren_SetSceneCamera(RenScene *scene, const RenCameraDesc *desc) {
  assert(scene);
  assert(desc);
  scene->set_camera(*desc);
}

RenResult ren_CreateMeshInstance(RenScene *scene,
                                 const RenMeshInstanceDesc *desc,
                                 RenMeshInstance *p_model) {
  assert(scene);
  assert(desc);
  return lippincott([&] { *p_model = scene->create_model(*desc); });
}

void ren_DestroyMeshInstance(RenScene *scene, RenMeshInstance model) {
  assert(scene);
  scene->destroy_model(model);
}

void ren_SetMeshInstanceMatrix(RenScene *scene, RenMeshInstance model,
                               const RenMatrix4x4 *matrix) {
  assert(scene);
  assert(matrix);
  scene->set_model_matrix(model, glm::make_mat4(*matrix));
}

RenResult ren_CreateDirectionalLight(RenScene *scene,
                                     const RenDirectionalLightDesc *desc,
                                     RenDirectionalLight *p_light) {
  assert(scene);
  assert(desc);
  assert(p_light);
  return lippincott([&] { *p_light = scene->create_dir_light(*desc); });
}

void ren_DestroyDirectionalLight(RenScene *scene, RenDirectionalLight light) {
  assert(scene);
  lippincott([&] { scene->destroy_dir_light(light); });
}

void ren_SetDirectionalLightColor(RenScene *scene, RenDirectionalLight light,
                                  RenVector3 color) {
  assert(scene);
  scene->get_dir_light(light).color = glm::make_vec3(color);
}

void ren_SetDirectionalLightIntencity(RenScene *scene,
                                      RenDirectionalLight light,
                                      float intencity) {
  assert(scene);
  scene->get_dir_light(light).illuminance = intencity;
}

void ren_SetDirectionalLightOrigin(RenScene *scene, RenDirectionalLight light,
                                   RenVector3 origin) {
  assert(scene);
  scene->get_dir_light(light).origin = glm::make_vec3(origin);
}
}
