#include "Device.hpp"
#include "Scene.hpp"
#include "Swapchain.hpp"
#include "ren/ren-vk.h"

#include <cassert>

extern "C" {

uint32_t ren_vk_GetRequiredAPIVersion() {
  return Device::getRequiredAPIVersion();
}

size_t ren_vk_GetRequiredLayerCount() {
  return Device::getRequiredLayers().size();
}

const char *const *ren_vk_GetRequiredLayers() {
  return Device::getRequiredLayers().data();
}

size_t ren_vk_GetRequiredExtensionCount() {
  return Device::getRequiredExtensions().size();
}

const char *const *ren_vk_GetRequiredExtensions() {
  return Device::getRequiredExtensions().data();
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

Scene *ren_CreateScene(RenDevice *device) {
  assert(device);
  return new Scene(device);
}

void ren_DestroyScene(Scene *scene) { delete scene; }

void ren_SceneBeginFrame(Scene *scene) {
  assert(scene);
  scene->begin_frame();
}

void ren_SceneEndFrame(Scene *scene) {
  assert(scene);
  scene->end_frame();
}

void ren_SceneDraw(Scene *scene) { scene->draw(); }

void ren_SetSceneOutputSize(Scene *scene, unsigned width, unsigned height) {
  scene->setOutputSize(width, height);
}

unsigned ren_GetSceneOutputWidth(const Scene *scene) {
  return scene->getOutputWidth();
}

unsigned ren_GetSceneOutputHeight(const Scene *scene) {
  return scene->getOutputHeight();
}

void ren_SetSceneSwapchain(Scene *scene, RenSwapchain *swapchain) {
  scene->setSwapchain(swapchain);
}

MeshID ren_CreateMesh(Scene *scene, const MeshDesc *desc) {
  assert(scene);
  assert(desc);
  return scene->create_mesh(*desc);
}

void ren_DestroyMesh(Scene *scene, MeshID mesh) {
  assert(scene);
  scene->destroy_mesh(mesh);
}

MaterialID ren_CreateMaterial(Scene *scene, const MaterialDesc *desc) {
  assert(scene);
  assert(desc);
  return scene->create_material(*desc);
}

void ren_DestroyMaterial(Scene *scene, MaterialID material) {
  assert(scene);
  scene->destroy_material(material);
}

void ren_SetSceneCamera(Scene *scene, const CameraDesc *desc) {
  assert(scene);
  assert(desc);
  scene->set_camera(*desc);
}

ModelID ren_CreateModel(Scene *scene, const ModelDesc *desc) {
  assert(scene);
  assert(desc);
  return scene->create_model(*desc);
}

void ren_DestroyModel(Scene *scene, ModelID model) {
  assert(scene);
  scene->destroy_model(model);
}

void ren_SetModelMatrix(Scene *scene, ModelID model, const float matrix[16]) {
  assert(scene);
  assert(matrix);
  scene->set_model_matrix(model, glm::make_mat4(matrix));
}
}
