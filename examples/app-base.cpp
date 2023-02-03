#include "app-base.hpp"
#include "ren/ren-vk.hpp"

#include <SDL2/SDL_vulkan.h>
#include <fmt/format.h>

#include <cassert>
#include <charconv>
#include <optional>
#include <vector>

namespace {

std::string env(const char *var) {
  auto *val = std::getenv(var);
  return val ? val : "";
}

VkInstance createInstance() {
  VkInstanceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
  };

  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&create_info, nullptr, &instance)) {
    throw std::runtime_error{"Vulkan: Failed to create VkInstance"};
  }

  return instance;
}

VkPhysicalDevice selectAdapter(VkInstance instance, unsigned idx = 0) {
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

ren::UniqueDevice create_device(VkPhysicalDevice adapter) {
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(adapter, &props);
  fmt::print("Running on {}\n", props.deviceName);

  unsigned num_extensions = 0;
  if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &num_extensions, nullptr)) {
    throw std::runtime_error("SDL_Vulkan: Failed to query instance extensions");
  }
  std::vector<const char *> extensions(num_extensions);
  if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &num_extensions,
                                        extensions.data())) {
    throw std::runtime_error("SDL_Vulkan: Failed to query instance extensions");
  }
  extensions.resize(num_extensions);

  ren::vk::DeviceDesc desc = {
      .proc = vkGetInstanceProcAddr,
      .instance_extensions = extensions,
  };
  std::memcpy(desc.pipeline_cache_uuid, props.pipelineCacheUUID,
              sizeof(props.pipelineCacheUUID));

  return ren::vk::Device::create(desc).value();
}

ren::UniqueSwapchain create_swapchain(SDL_Window *window, ren::Device &device) {
  auto &vk_device = static_cast<ren::vk::Device &>(device);
  return vk_device
      .create_swapchain([=](VkInstance instance, VkSurfaceKHR *p_surface) {
        if (!SDL_Vulkan_CreateSurface(window, instance, p_surface)) {
          return VK_ERROR_UNKNOWN;
        }
        return VK_SUCCESS;
      })
      .value();
}

} // namespace

AppBase::AppBase(std::string app_name) : m_app_name(std::move(app_name)) {
  m_window.reset(SDL_CreateWindow(m_app_name.c_str(), SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED, m_window_width,
                                  m_window_height,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN));

  std::unique_ptr<std::remove_pointer_t<VkInstance>,
                  decltype([](VkInstance instance) {
                    vkDestroyInstance(instance, nullptr);
                  })>
      instance(createInstance());

  std::string ren_adapter = env("REN_ADAPTER");
  unsigned adapter_idx = 0;
  auto [end, ec] = std::from_chars(
      ren_adapter.data(), ren_adapter.data() + ren_adapter.size(), adapter_idx);
  if (end != ren_adapter.data() + ren_adapter.size() or
      ec != std::error_code()) {
    adapter_idx = 0;
  };
  auto adapter = selectAdapter(instance.get(), adapter_idx);
  if (!adapter) {
    throw std::runtime_error{"Failed to find adapter"};
  }

  m_device = create_device(adapter);

  m_swapchain = create_swapchain(m_window.get(), *m_device);

  m_scene = m_device->create_scene().value();
}

void AppBase::run() {
  bool quit = false;
  while (!quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        quit = true;
      }
      process_event(e);
    }

    {
      int w, h;
      SDL_GetWindowSize(m_window.get(), &w, &h);
      m_window_width = w;
      m_window_height = h;
    }
    m_swapchain->set_size(m_window_width, m_window_height);
    m_scene->set_viewport(m_window_width, m_window_height).value();

    iterate();

    m_scene->draw(*m_swapchain).value();
  }
}
