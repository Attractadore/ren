#pragma once
#include "Def.hpp"
#include "Descriptors.hpp"

namespace ren {

class DescriptorSetAllocator {
public:
  DescriptorSetAllocator(Device &device);

  void begin_frame();
  void end_frame();

  DescriptorSet allocate(const DescriptorSetLayoutRef &layout);
};

} // namespace ren
