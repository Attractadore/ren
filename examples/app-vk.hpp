#pragma once
#include "app-base.hpp"
#include "ren/ren-vk.hpp"

#include <SDL2/SDL_vulkan.h>

inline VkInstance createInstance() {
  VkApplicationInfo application_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .apiVersion = ren::vk::get_required_api_version(),
  };

  auto layers = ren::vk::get_required_layers();

  unsigned ext_cnt = 0;
  if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &ext_cnt, nullptr)) {
    return nullptr;
  }
  std::vector<const char *> extensions(ext_cnt);
  if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &ext_cnt, extensions.data())) {
    return nullptr;
  }
  extensions.resize(ext_cnt);
  {
    auto req_extensions = ren::vk::get_required_extensions();
    extensions.insert(extensions.end(), req_extensions.begin(),
                      req_extensions.end());
  }

  VkInstanceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &application_info,
      .enabledLayerCount = static_cast<uint32_t>(layers.size()),
      .ppEnabledLayerNames = layers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };

  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&create_info, nullptr, &instance)) {
    throw std::runtime_error{"Vulkan: Failed to create VkInstance"};
  }

  return instance;
}

inline VkPhysicalDevice selectAdapter(VkInstance instance, unsigned idx = 0) {
  unsigned dev_cnt = 0;
  if (vkEnumeratePhysicalDevices(instance, &dev_cnt, nullptr)) {
    throw std::runtime_error{"Vulkan: Failed to enumerate devices"};
  }
  std::vector<VkPhysicalDevice> devs(dev_cnt);
  if (vkEnumeratePhysicalDevices(instance, &dev_cnt, devs.data())) {
    throw std::runtime_error{"Vulkan: Failed to enumerate devices"};
  }
  devs.resize(dev_cnt);
  if (idx < dev_cnt) {
    return devs[idx];
  }
  return VK_NULL_HANDLE;
}

class VulkanRenderer final : public Renderer {
  struct InstanceDeleter {
    void operator()(VkInstance instance) const noexcept {
      vkDestroyInstance(instance, nullptr);
    }
  };

  class SurfaceDeleter {
    VkInstance m_instance;

  public:
    SurfaceDeleter() : m_instance(VK_NULL_HANDLE) {}
    SurfaceDeleter(VkInstance instance) : m_instance(instance) {}
    void operator()(VkSurfaceKHR surface) const {
      if (surface) {
        assert(m_instance);
        vkDestroySurfaceKHR(m_instance, surface, nullptr);
      }
    }
  };

  std::unique_ptr<std::remove_pointer_t<VkInstance>, InstanceDeleter>
      m_instance;
  VkPhysicalDevice m_adapter = VK_NULL_HANDLE;
  std::unique_ptr<std::remove_pointer_t<VkSurfaceKHR>, SurfaceDeleter>
      m_surface;

public:
  Uint32 get_SDL2_flags() const override { return SDL_WINDOW_VULKAN; }

  void create_instance() override {
    std::cout << "Create VkInstance\n";
    m_instance.reset(createInstance());
  }

  bool select_adapter(unsigned idx) override {
    std::cout << "Select VkPhysicalDevice #" << idx << "\n";
    m_adapter = selectAdapter(m_instance.get(), idx);
    return m_adapter;
  }

  std::string get_adapter_name() const override {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_adapter, &props);
    return props.deviceName;
  }

  ren::UniqueDevice create_device() override {
    std::cout << "Create ren::vk::Device\n";
    return ren::vk::Device::create(m_instance.get(), m_adapter);
  }

  ren::UniqueSwapchain create_swapchain(ren::Device &device,
                                        SDL_Window *window) override {
    std::cout << "Create VkSurfaceKHR\n";
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, m_instance.get(), &surface)) {
      throw std::runtime_error{"SDL_Vulkan: Failed to create VkSurfaceKHR"};
    }
    m_surface = {surface, SurfaceDeleter(m_instance.get())};
    auto *vk_device = static_cast<ren::vk::Device *>(&device);
    std::cout << "Create ren::vk::Swapchain\n";
    return vk_device->create_swapchain(surface);
  }
};
