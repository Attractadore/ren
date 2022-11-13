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

enum class CameraID : RenCameraID;

struct CameraRef {
  RenScene *m_scene;
  RenCameraID m_camera;

public:
  CameraRef() : CameraRef(nullptr, CameraID(0)) {}
  CameraRef(Scene *scene, CameraID camera)
      : m_scene(reinterpret_cast<RenScene *>(scene)),
        m_camera(std::bit_cast<RenCameraID>(camera)) {}

  void activate() { ren_SetSceneCamera(m_scene, m_camera); }

  Scene *getScene() { return reinterpret_cast<Scene *>(m_scene); };
  const Scene *getScene() const {
    return reinterpret_cast<const Scene *>(m_scene);
  };
  auto getID() const { return std::bit_cast<CameraID>(m_camera); }
};

struct Scene : RenScene {
  void setPipelineDepth(unsigned pipeline_depth) {
    ren_SetScenePipelineDepth(this, pipeline_depth);
  }

  auto getPipelineDepth() const { return ren_GetScenePipelineDepth(this); }

  CameraRef createCamera() {
    return getCamera(std::bit_cast<CameraID>(ren_CreateCamera(this)));
  }

  void destroyCamera(CameraID camera) {
    ren_DestroyCamera(this, std::bit_cast<RenCameraID>(camera));
  }

  void setCamera(CameraID camera) {
    ren_SetSceneCamera(this, std::bit_cast<RenCameraID>(camera));
  }

  void setSwapchain(Swapchain *swapchain) {
    ren_SetSceneSwapchain(this, swapchain);
  }

  CameraRef getCamera(CameraID camera) { return {this, camera}; }

  void setOutputSize(unsigned width, unsigned height) {
    ren_SetSceneOutputSize(this, width, height);
  }

  auto getOutputWidth() const { return ren_GetSceneOutputWidth(this); }
  auto getOutputHeight() const { return ren_GetSceneOutputHeight(this); }

  void draw() { ren_DrawScene(this); }
};
} // namespace v0
} // namespace ren
