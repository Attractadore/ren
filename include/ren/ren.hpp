#pragma once
#include "ren.h"

#include <bit>
#include <memory>

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
struct Device;
using DeviceDeleter = decltype([](Device *device) {
  ren_DestroyDevice(reinterpret_cast<RenDevice *>(device));
});
using UniqueDevice = std::unique_ptr<Device, DeviceDeleter>;
using SharedDevice = std::shared_ptr<Device>;

struct Swapchain;
using SwapchainDeleter = decltype([](Swapchain *swapchain) {
  ren_DestroySwapchain(reinterpret_cast<RenSwapchain *>(swapchain));
});
using UniqueSwapchain = std::unique_ptr<Swapchain, SwapchainDeleter>;
using SharedSwapchain = std::shared_ptr<Swapchain>;

struct Scene;
using SceneDeleter = decltype([](Scene *scene) {
  ren_DestroyScene(reinterpret_cast<RenScene *>(scene));
});
using UniqueScene = std::unique_ptr<Scene, SceneDeleter>;
using SharedScene = std::shared_ptr<Scene>;

struct Device : RenDevice {
  UniqueScene createScene() {
    return {reinterpret_cast<Scene *>(ren_CreateScene(this)), SceneDeleter()};
  }
};

struct Swapchain : RenSwapchain {
  void setSize(unsigned width, unsigned height) {
    ren_SetSwapchainSize(this, width, height);
  }

  auto getWidth() const { return ren_GetSwapchainWidth(this); }
  auto getHeight() const { return ren_GetSwapchainHeight(this); }
};

struct CameraRef {
  RenScene *m_scene;

public:
  CameraRef() : CameraRef(nullptr) {}
  CameraRef(Scene *scene) : m_scene(reinterpret_cast<RenScene *>(scene)) {}

  Scene *getScene() { return reinterpret_cast<Scene *>(m_scene); };
  const Scene *getScene() const {
    return reinterpret_cast<const Scene *>(m_scene);
  };
};

struct Scene : RenScene {
  void setPipelineDepth(unsigned pipeline_depth) {
    ren_SetScenePipelineDepth(this, pipeline_depth);
  }

  auto getPipelineDepth() const { return ren_GetScenePipelineDepth(this); }

  void setSwapchain(Swapchain *swapchain) {
    ren_SetSceneSwapchain(this, swapchain);
  }

  CameraRef getCamera() { return {this}; }

  void setOutputSize(unsigned width, unsigned height) {
    ren_SetSceneOutputSize(this, width, height);
  }

  auto getOutputWidth() const { return ren_GetSceneOutputWidth(this); }
  auto getOutputHeight() const { return ren_GetSceneOutputHeight(this); }

  void draw() { ren_DrawScene(this); }
};
} // namespace v0
} // namespace ren
