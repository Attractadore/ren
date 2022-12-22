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

private:
  void create();
  void destroy();

public:
  VulkanSwapchain(VulkanDevice *device, VkSurfaceKHR surface);
  VulkanSwapchain(const VulkanSwapchain &) = delete;
  VulkanSwapchain(VulkanSwapchain &&);
  VulkanSwapchain &operator=(const VulkanSwapchain &) = delete;
  VulkanSwapchain &operator=(VulkanSwapchain &&);
  ~VulkanSwapchain();

  std::pair<unsigned, unsigned> get_size() const override {
    return {m_create_info.imageExtent.width, m_create_info.imageExtent.height};
  }
  void setSize(unsigned width, unsigned height) override;

  VkPresentModeKHR get_present_mode() const {
    return m_create_info.presentMode;
  }
  void set_present_mode(VkPresentModeKHR);

  VkSurfaceKHR get_surface() const { return m_create_info.surface; }

  void acquireImage(VkSemaphore signal_semaphore);
  void presentImage(VkSemaphore wait_semaphore);
  const Texture &getTexture() {
    assert(m_image_index < m_textures.size());
    return m_textures[m_image_index];
  }
};
} // namespace ren
