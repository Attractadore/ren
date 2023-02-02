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

RenResult ren_vk_CreateDevice(PFN_vkGetInstanceProcAddr proc,
                              VkInstance instance, VkPhysicalDevice adapter,
                              RenDevice **p_device) {
  assert(proc);
  assert(instance);
  assert(adapter);
  assert(p_device);
  return lippincott(
      [&] { *p_device = new RenDevice(proc, instance, adapter); });
}

void ren_DestroyDevice(RenDevice *device) { delete device; }

RenResult ren_vk_CreateSwapchain(RenDevice *device, VkSurfaceKHR surface,
                                 RenSwapchain **p_swapchain) {
  assert(device);
  assert(surface);
  assert(p_swapchain);
  return lippincott([&] { *p_swapchain = new RenSwapchain(*device, surface); });
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
}
