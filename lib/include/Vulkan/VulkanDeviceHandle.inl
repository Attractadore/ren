#pragma once
#include "VulkanDevice.hpp"
#include "VulkanDeviceHandle.hpp"

#include <cassert>
#include <concepts>

namespace ren {
namespace detail {
template <typename T> void VulkanHandleDeleter<T>::operator()(T handle) const {
  if (!handle) {
    return;
  }
  assert(m_device);
  if constexpr (std::same_as<T, VkCommandPool>) {
    m_device->pushToDeleteQueue(
        [=](VulkanDevice &device) { device.DestroyCommandPool(handle); });
  }
}
} // namespace detail
} // namespace ren
