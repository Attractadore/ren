#include "DescriptorSetAllocator.hpp"
#include "Support/Errors.hpp"

namespace ren {

DescriptorSetAllocator::DescriptorSetAllocator(Device &device) { todo(); }

void DescriptorSetAllocator::begin_frame() { todo(); }

void DescriptorSetAllocator::end_frame() { todo(); }

auto DescriptorSetAllocator::allocate(const DescriptorSetLayoutRef &layout)
    -> DescriptorSet {
  todo();
}

} // namespace ren
