#pragma once
#include <vulkan/vulkan.h>

#include <memory>

namespace ren {
class VulkanDevice;

namespace detail {
template <typename T> class VulkanHandleDeleter {
  VulkanDevice *m_device;

public:
  VulkanHandleDeleter() : m_device(nullptr) {}
  VulkanHandleDeleter(VulkanDevice &device) : m_device(&device) {}

  VulkanDevice *getDevice() const { return m_device; }

  void operator()(T handle) const;
};
} // namespace detail

template <typename T>
struct VulkanDeviceHandle : std::unique_ptr<std::remove_pointer_t<T>,
                                            detail::VulkanHandleDeleter<T>> {
  VulkanDeviceHandle() = default;
  VulkanDeviceHandle(T handle, VulkanDevice &device)
      : std::unique_ptr<std::remove_pointer_t<T>,
                        detail::VulkanHandleDeleter<T>>::
            unique_ptr(handle, detail::VulkanHandleDeleter<T>(device)) {}

  VulkanDevice *getDevice() const { return this->get_deleter().getDevice(); }
};
} // namespace ren
