#pragma once
#include "Def.hpp"

class RenScene {
  ren::Device *m_device;

  unsigned m_output_width = 0;
  unsigned m_output_height = 0;
  ren::Swapchain *m_swapchain;

public:
  RenScene(ren::Device *device);

  void setOutputSize(unsigned width, unsigned height);
  unsigned getOutputWidth() const { return m_output_width; }
  unsigned getOutputHeight() const { return m_output_height; }

  void setSwapchain(ren::Swapchain *swapchain);
  ren::Swapchain *getSwapchain() const { return m_swapchain; }

  void draw();
};
