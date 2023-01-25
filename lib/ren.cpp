#include "Device.hpp"
#include "Scene.hpp"
#include "Swapchain.hpp"
#include "ren/ren-vk.h"

#include <cassert>

extern "C" {

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

RenDevice *ren_vk_CreateDevice(PFN_vkGetInstanceProcAddr proc,
                               VkInstance instance,
                               VkPhysicalDevice m_adapter) {
  return new RenDevice(proc, instance, m_adapter);
}

void ren_DestroyDevice(RenDevice *device) { delete device; }

void ren_DeviceBeginFrame(RenDevice *device) {
  assert(device);
  device->begin_frame();
}

void ren_DeviceEndFrame(RenDevice *device) {
  assert(device);
  device->end_frame();
}

RenSwapchain *ren_vk_CreateSwapchain(RenDevice *device, VkSurfaceKHR surface) {
  assert(device);
  assert(surface);
  return new RenSwapchain(*device, surface);
}

void ren_DestroySwapchain(RenSwapchain *swapchain) { delete swapchain; }

void ren_SetSwapchainSize(RenSwapchain *swapchain, unsigned width,
                          unsigned height) {
  swapchain->set_size(width, height);
}

VkSurfaceKHR ren_vk_GetSwapchainSurface(const RenSwapchain *swapchain) {
  assert(swapchain);
  return swapchain->get_surface();
}

VkPresentModeKHR ren_vk_GetSwapchainPresentMode(const RenSwapchain *swapchain) {
  assert(swapchain);
  return swapchain->get_present_mode();
}

void ren_vk_SetSwapchainPresentMode(RenSwapchain *swapchain,
                                    VkPresentModeKHR present_mode) {
  assert(swapchain);
  swapchain->set_present_mode(present_mode);
}

unsigned ren_GetSwapchainWidth(const RenSwapchain *swapchain) {
  return swapchain->get_size().first;
}

unsigned ren_GetSwapchainHeight(const RenSwapchain *swapchain) {
  return swapchain->get_size().second;
}

RenScene *ren_CreateScene(RenDevice *device) {
  assert(device);
  return new RenScene(*device);
}

void ren_DestroyScene(RenScene *scene) { delete scene; }

void ren_SceneBeginFrame(RenScene *scene, RenSwapchain *swapchain) {
  assert(scene);
  assert(swapchain);
  scene->setSwapchain(*swapchain);
  scene->begin_frame();
}

void ren_SceneEndFrame(RenScene *scene) {
  assert(scene);
  scene->draw();
  scene->end_frame();
}

void ren_SceneDraw(RenScene *scene) { scene->draw(); }

void ren_SetSceneOutputSize(RenScene *scene, unsigned width, unsigned height) {
  scene->setOutputSize(width, height);
}

unsigned ren_GetSceneOutputWidth(const RenScene *scene) {
  return scene->getOutputWidth();
}

unsigned ren_GetSceneOutputHeight(const RenScene *scene) {
  return scene->getOutputHeight();
}

RenMesh ren_CreateMesh(RenScene *scene, const RenMeshDesc *desc) {
  assert(scene);
  assert(desc);
  return scene->create_mesh(*desc);
}

void ren_DestroyMesh(RenScene *scene, RenMesh mesh) {
  assert(scene);
  scene->destroy_mesh(mesh);
}

RenMaterial ren_CreateMaterial(RenScene *scene, const RenMaterialDesc *desc) {
  assert(scene);
  assert(desc);
  return scene->create_material(*desc);
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

RenModel ren_CreateModel(RenScene *scene, const RenModelDesc *desc) {
  assert(scene);
  assert(desc);
  return scene->create_model(*desc);
}

void ren_DestroyModel(RenScene *scene, RenModel model) {
  assert(scene);
  scene->destroy_model(model);
}

void ren_SetModelMatrix(RenScene *scene, RenModel model,
                        const float matrix[16]) {
  assert(scene);
  assert(matrix);
  scene->set_model_matrix(model, glm::make_mat4(matrix));
}
}
