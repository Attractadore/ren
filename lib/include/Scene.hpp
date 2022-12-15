#pragma once
#include "CommandBuffer.hpp"
#include "Def.hpp"

namespace ren {
class CommandAllocator;
}

class RenScene {
  ren::Device *m_device;

  unsigned m_output_width = 0;
  unsigned m_output_height = 0;
  ren::Swapchain *m_swapchain;

  std::unique_ptr<ren::CommandAllocator> m_cmd_pool;

public:
  RenScene(ren::Device *device);
  ~RenScene();

  void setOutputSize(unsigned width, unsigned height);
  unsigned getOutputWidth() const { return m_output_width; }
  unsigned getOutputHeight() const { return m_output_height; }

  void setPipelineDepth(unsigned pipeline_depth);
  unsigned getPipelineDepth() const;

  void setSwapchain(ren::Swapchain *swapchain);
  ren::Swapchain *getSwapchain() const { return m_swapchain; }

  void draw();
};
