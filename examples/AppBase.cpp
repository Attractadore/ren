#include "AppBase.hpp"
#include "ren/ren-vk.hpp"

#include <SDL2/SDL_vulkan.h>
#include <fmt/format.h>

#include <cassert>
#include <charconv>
#include <optional>
#include <utility>
#include <vector>

namespace chrono = std::chrono;

namespace {

std::string env(const char *var) {
  auto *val = std::getenv(var);
  return val ? val : "";
}

using Instance = std::unique_ptr<std::remove_pointer_t<VkInstance>,
                                 decltype([](VkInstance instance) {
                                   vkDestroyInstance(instance, nullptr);
                                 })>;

auto create_instance() -> Result<Instance> {
  VkInstanceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
  };

  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&create_info, nullptr, &instance)) {
    bail("Vulkan: Failed to create VkInstance");
  }

  return Instance(instance);
}

auto select_adapter(VkInstance instance, unsigned idx = 0)
    -> Result<VkPhysicalDevice> {
  unsigned dev_cnt = 0;
  if (vkEnumeratePhysicalDevices(instance, &dev_cnt, nullptr)) {
    bail("Vulkan: Failed to enumerate devices");
  }
  std::vector<VkPhysicalDevice> devs(dev_cnt);
  if (vkEnumeratePhysicalDevices(instance, &dev_cnt, devs.data())) {
    bail("Vulkan: Failed to enumerate devices");
  }
  devs.resize(dev_cnt);
  if (idx < dev_cnt) {
    return devs[idx];
  }
  return VK_NULL_HANDLE;
}

auto create_device(VkPhysicalDevice adapter) -> Result<ren::UniqueDevice> {
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(adapter, &props);
  fmt::print("Running on {}\n", props.deviceName);

  unsigned num_extensions = 0;
  if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &num_extensions, nullptr)) {
    bail("SDL_Vulkan: Failed to query instance extensions");
  }
  std::vector<const char *> extensions(num_extensions);
  if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &num_extensions,
                                        extensions.data())) {
    bail("SDL_Vulkan: Failed to query instance extensions");
  }
  extensions.resize(num_extensions);

  ren::vk::DeviceDesc desc = {
      .proc = vkGetInstanceProcAddr,
      .instance_extensions = extensions,
  };
  std::memcpy(desc.pipeline_cache_uuid, props.pipelineCacheUUID,
              sizeof(props.pipelineCacheUUID));

  return ren::vk::Device::create(desc).transform_error(get_error_string);
}

auto create_swapchain(SDL_Window *window, ren::Device &device)
    -> Result<ren::UniqueSwapchain> {
  return ren::vk::Swapchain::create(
             device,
             [=](VkInstance instance, VkSurfaceKHR *p_surface) {
               if (!SDL_Vulkan_CreateSurface(window, instance, p_surface)) {
                 return VK_ERROR_UNKNOWN;
               }
               return VK_SUCCESS;
             })
      .transform_error(get_error_string);
}

} // namespace

auto get_error_string_impl(std::string err) -> std::string { return err; }

auto get_error_string_impl(ren::Error err) -> std::string {
  switch (err) {
  case ren::Error::Vulkan:
    return "ren: Vulkan error";
  case ren::Error::System:
    return "ren: System error";
  case ren::Error::Runtime:
    return "ren: Runtime error";
  case ren::Error::Unknown:
    return "ren: Unknown error";
  }
  std::unreachable();
}

auto throw_error(std::string err) -> std::string {
  throw std::runtime_error(std::move(err));
}

AppBase::AppBase(const char *app_name) {
  [&]() -> Result<void> {
    m_window.reset(SDL_CreateWindow(app_name, SDL_WINDOWPOS_UNDEFINED,
                                    SDL_WINDOWPOS_UNDEFINED, m_window_width,
                                    m_window_height,
                                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN));
    if (!m_window) {
      bail("{}", SDL_GetError());
    }

    OK(auto instance, create_instance());

    std::string ren_adapter = env("REN_ADAPTER");
    unsigned adapter_idx = 0;
    auto [end, ec] =
        std::from_chars(ren_adapter.data(),
                        ren_adapter.data() + ren_adapter.size(), adapter_idx);
    if (end != ren_adapter.data() + ren_adapter.size() or
        ec != std::error_code()) {
      adapter_idx = 0;
    };
    OK(auto adapter, select_adapter(instance.get(), adapter_idx));
    if (!adapter) {
      bail("Failed to find adapter");
    }

    OK(m_device, create_device(adapter));

    OK(m_swapchain, create_swapchain(m_window.get(), *m_device));

    OK(m_scene, ren::Scene::create(*m_device, *m_swapchain));

    return {};
  }()
               .transform_error(throw_error);
}

auto AppBase::loop() -> Result<void> {
  auto last_time = chrono::steady_clock::now();
  bool quit = false;

  while (!quit) {
    auto now = chrono::steady_clock::now();
    chrono::nanoseconds dt = now - last_time;
    last_time = now;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT or
          (e.type == SDL_KEYDOWN and
           e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)) {
        quit = true;
      }
      TRY_TO(process_event(e));
    }

    {
      int w, h;
      SDL_Vulkan_GetDrawableSize(m_window.get(), &w, &h);
      m_window_width = w;
      m_window_height = h;
      m_swapchain->set_size(m_window_width, m_window_height);
    }

    TRY_TO(iterate(m_window_width, m_window_height, dt));
    TRY_TO(m_scene->draw());
  }

  return {};
}

auto AppBase::get_scene() const -> const ren::Scene & { return *m_scene; }
auto AppBase::get_scene() -> ren::Scene & { return *m_scene; }

auto AppBase::process_event(const SDL_Event &e) -> Result<void> { return {}; }

auto AppBase::iterate(unsigned width, unsigned height, std::chrono::nanoseconds)
    -> Result<void> {
  auto &scene = get_scene();

  TRY_TO(scene.set_camera({
      .width = width,
      .height = height,
  }));

  return {};
}
