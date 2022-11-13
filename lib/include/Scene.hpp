#pragma once
#include "CommandBuffer.hpp"
#include "Def.hpp"
#include "Support/SlotMap.hpp"

namespace ren {
class CommandAllocator;

struct Camera {};

class Cameras {
  using CameraMap = SlotMap<Camera>;
  using CameraKey = CameraMap::key_type;
  CameraMap m_cameras;

public:
  CameraID create();
  void destroy(CameraID camera);

  auto begin() { return m_cameras.values().begin(); }
  auto begin() const { return m_cameras.values().begin(); }
  auto end() { return m_cameras.values().end(); }
  auto end() const { return m_cameras.values().end(); }

private:
  Camera &getCamera(CameraID camera);
};
} // namespace ren

class RenScene {
  ren::Device *m_device;
  ren::CameraID m_default_camera;
  ren::Cameras m_cameras;

  unsigned m_output_width = 0;
  unsigned m_output_height = 0;
  ren::Swapchain *m_swapchain;

  std::unique_ptr<ren::CommandAllocator> m_cmd_pool;

public:
  RenScene(ren::Device *device);
  ~RenScene();

  ren::CameraID createCamera() { return m_cameras.create(); }
  void destroyCamera(ren::CameraID camera) { m_cameras.destroy(camera); }

  void setOutputSize(unsigned width, unsigned height);
  unsigned getOutputWidth() const { return m_output_width; }
  unsigned getOutputHeight() const { return m_output_height; }

  void setPipelineDepth(unsigned pipeline_depth);
  unsigned getPipelineDepth() const;

  void setSwapchain(ren::Swapchain *swapchain);
  ren::Swapchain *getSwapchain() const { return m_swapchain; }

  ren::CameraID getDefaultCamera() const;
  void setCamera(ren::CameraID camera);
  ren::CameraID getCamera() const;

  void draw();
};
