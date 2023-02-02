#include "app-base.hpp"
#include "ren/ren-vk.hpp"

#include <SDL2/SDL_vulkan.h>
#include <fmt/format.h>

#include <cassert>
#include <charconv>
#include <vector>

namespace {

std::string env(const char *var) {
  auto *val = std::getenv(var);
  return val ? val : "";
}

VkInstance createInstance() {
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

} // namespace

struct AppBase::VulkanData {
  std::unique_ptr<std::remove_pointer_t<VkInstance>, InstanceDeleter> instance;
  VkPhysicalDevice adapter = VK_NULL_HANDLE;
  std::unique_ptr<std::remove_pointer_t<VkSurfaceKHR>, SurfaceDeleter> surface;
};

AppBase::AppBase(std::string app_name) : m_app_name(std::move(app_name)) {
  m_vk = std::make_unique<VulkanData>();

  fmt::print("Create SDL_Window\n");
  m_window.reset(SDL_CreateWindow(m_app_name.c_str(), SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED, m_window_width,
                                  m_window_height,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN));

  fmt::print("Create VkInstance\n");
  m_vk->instance.reset(createInstance());

  std::string ren_adapter = env("REN_ADAPTER");
  unsigned adapter_idx = 0;
  auto [end, ec] = std::from_chars(
      ren_adapter.data(), ren_adapter.data() + ren_adapter.size(), adapter_idx);
  if (end != ren_adapter.data() + ren_adapter.size() or
      ec != std::error_code()) {
    adapter_idx = 0;
  };
  fmt::print("Select VkPhysicalDevice #{}\n", adapter_idx);
  m_vk->adapter = selectAdapter(m_vk->instance.get(), adapter_idx);
  if (!m_vk->adapter) {
    throw std::runtime_error{"Failed to find adapter"};
  }
  {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_vk->adapter, &props);
    fmt::print("Running on {}\n", props.deviceName);
  }

  fmt::print("Create ren::Device\n");
  m_device =
      ren::vk::Device::create(m_vk->instance.get(), m_vk->adapter).value();

  fmt::print("Create VkSurfaceKHR\n");
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(m_window.get(), m_vk->instance.get(),
                                &surface)) {
    throw std::runtime_error{"SDL_Vulkan: Failed to create VkSurfaceKHR"};
  }
  m_vk->surface = {surface, SurfaceDeleter(m_vk->instance.get())};
  auto *vk_device = static_cast<ren::vk::Device *>(m_device.get());
  fmt::print("Create ren::Swapchain\n");
  m_swapchain = vk_device->create_swapchain(surface).value();

  fmt::print("Create ren::Scene\n");
  m_scene = m_device->create_scene().value();
}

AppBase::AppBase(AppBase &&) noexcept = default;
AppBase &AppBase::operator=(AppBase &&) noexcept = default;
AppBase::~AppBase() = default;

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

    iterate(*m_scene);

    m_scene->draw(*m_swapchain, m_window_width, m_window_height).value();
  }

  fmt::print("Done\n");
}
