#include "Device.hpp"
#include "Scene.hpp"
#include "Swapchain.hpp"

#include <cassert>

using namespace ren;

extern "C" {

void ren_DeviceBeginFrame(Device *device) {
  assert(device);
  device->begin_frame();
}

void ren_DeviceEndFrame(Device *device) {
  assert(device);
  device->end_frame();
}

void ren_DestroyDevice(Device *device) { delete device; }

void ren_DestroySwapchain(RenSwapchain *swapchain) { delete swapchain; }

void ren_SetSwapchainSize(RenSwapchain *swapchain, unsigned width,
                          unsigned height) {
  swapchain->set_size(width, height);
}

unsigned ren_GetSwapchainWidth(const RenSwapchain *swapchain) {
  return swapchain->get_size().first;
}

unsigned ren_GetSwapchainHeight(const RenSwapchain *swapchain) {
  return swapchain->get_size().second;
}

Scene *ren_CreateScene(Device *device) {
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
