#pragma once
#include "Def.hpp"

#include <utility>

struct RenSwapchain {
  virtual ~RenSwapchain() = default;

  virtual std::pair<unsigned, unsigned> get_size() const = 0;
  virtual void setSize(unsigned width, unsigned height) = 0;
};
