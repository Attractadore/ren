#pragma once
#include "Support/Vector.hpp"
#include "Swapchain.hpp"
#include "VulkanTexture.hpp"
#include "ren/ren-vk.h"

namespace ren {
class VulkanDevice;

class VulkanSwapchain final : public Swapchain {
  VulkanDevice *m_device;
  VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
  SmallVector<Texture, 3> m_textures;
  unsigned m_image_index = -1;
  VkSwapchainCreateInfoKHR m_create_info;

public:
  VulkanSwapchain(VulkanDevice *device, VkSurfaceKHR surface);

  void setSize(unsigned width, unsigned height);

  void acquireImage(VkSemaphore signal_semaphore);
  void presentImage(VkSemaphore wait_semaphore);
  const Texture &getTexture() {
    assert(m_image_index < m_textures.size());
    return m_textures[m_image_index];
  }

private:
  void create();
};
} // namespace ren
