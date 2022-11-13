#include "Device.hpp"
#include "Scene.hpp"
#include "Swapchain.hpp"

#include <cassert>

using namespace ren;

extern "C" {
void ren_DestroyDevice(Device *device) { delete device; }

void ren_DestroySwapchain(Swapchain *swapchain) { delete swapchain; }

void ren_SetSwapchainSize(Swapchain *swapchain, unsigned width,
                          unsigned height) {
  swapchain->setSize(width, height);
}

Scene *ren_CreateScene(Device *device) {
  assert(device);
  return new Scene(device);
}

void ren_DestroyScene(Scene *scene) { delete scene; }

void ren_DrawScene(Scene *scene) { scene->draw(); }

CameraID ren_CreateCamera(Scene *scene) {
  assert(scene);
  return scene->createCamera();
}

void ren_SetSceneOutputSize(Scene *scene, unsigned width, unsigned height) {
  scene->setOutputSize(width, height);
}

void ren_SetSceneSwapchain(Scene *scene, Swapchain *swapchain) {
  scene->setSwapchain(swapchain);
}
}
