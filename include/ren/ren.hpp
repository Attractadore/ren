#pragma once
#include "ren.h"

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

struct Device : RenDevice {
  UniqueScene create_scene() {
    return {reinterpret_cast<Scene *>(ren_CreateScene(this)), SceneDeleter()};
  }
};

struct Swapchain : RenSwapchain {
  void set_size(unsigned width, unsigned height) {
    ren_SetSwapchainSize(this, width, height);
  }

  std::pair<unsigned, unsigned> get_size() const {
    return {ren_GetSwapchainWidth(this), ren_GetSwapchainHeight(this)};
  }
};

struct CameraRef {
  RenScene *m_scene;

public:
  CameraRef() : CameraRef(nullptr) {}
  CameraRef(Scene *scene) : m_scene(reinterpret_cast<RenScene *>(scene)) {}

  Scene *get_scene() { return reinterpret_cast<Scene *>(m_scene); };
  const Scene *get_scene() const {
    return reinterpret_cast<const Scene *>(m_scene);
  };
};

struct Scene : RenScene {
  void set_swapchain(Swapchain *swapchain) {
    ren_SetSceneSwapchain(this, swapchain);
  }

  CameraRef get_camera() { return {this}; }

  void set_output_size(unsigned width, unsigned height) {
    ren_SetSceneOutputSize(this, width, height);
  }

  std::pair<unsigned, unsigned> get_output_size() const {
    return {ren_GetSceneOutputWidth(this), ren_GetSceneOutputHeight(this)};
  }

  void draw() { ren_DrawScene(this); }
};
} // namespace v0
} // namespace ren
