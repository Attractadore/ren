#pragma once
#include "Def.hpp"

struct RenSwapchain {
  virtual ~RenSwapchain() = default;

  virtual void setSize(unsigned width, unsigned height) = 0;
};
