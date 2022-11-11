#pragma once
#include "Device.hpp"
#include "VulkanDispatchTable.hpp"

namespace Ren {
class VulkanDevice : public Device,
                     public InstanceFunctionsMixin<VulkanDevice>,
                     public PhysicalDeviceFunctionsMixin<VulkanDevice>,
                     public DeviceFunctionsMixin<VulkanDevice> {
  VkInstance m_instance;
  VkPhysicalDevice m_physical_device;
  VkDevice m_device;
  unsigned m_graphics_queue_family;
  VkQueue m_graphics_queue;
  VulkanDispatchTable m_vk;

public:
  VulkanDevice(PFN_vkGetInstanceProcAddr proc, VkInstance instance,
               VkPhysicalDevice physical_device);
  ~VulkanDevice();

  const VulkanDispatchTable &getDispatchTable() const { return m_vk; }

  VkInstance getInstance() const { return m_instance; }

  VkPhysicalDevice getPhysicalDevice() const { return m_physical_device; }

  VkDevice getDevice() const { return m_device; }

  const VkAllocationCallbacks *getAllocator() const { return nullptr; }
};
} // namespace Ren
