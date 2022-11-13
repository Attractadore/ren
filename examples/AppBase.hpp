#include "ren/ren-vk.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <cassert>
#include <iostream>
#include <vector>

inline VkInstance createInstance(const char *app_name) {
  auto layers = ren::vk::getRequiredLayers();

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
    auto req_extensions = ren::vk::getRequiredExtensions();
    extensions.insert(extensions.end(), req_extensions.begin(),
                      req_extensions.end());
  }

  VkApplicationInfo application_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = app_name,
      .apiVersion = ren::vk::getRequiredAPIVersion(),
  };

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
  if (devs.empty()) {
    throw std::runtime_error{"Vulkan: Failed to find a device"};
  }
  if (idx >= dev_cnt) {
    throw std::runtime_error{"Vulkan: Device index out of range"};
  }
  return devs[idx];
}

inline std::string getAdapterName(VkPhysicalDevice dev) {
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(dev, &props);
  return props.deviceName;
}

inline VkSurfaceKHR createSurface(VkInstance instance, SDL_Window *window) {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
    throw std::runtime_error{"SDL_Vulkan: Failed to create VkSurfaceKHR"};
  }
  return surface;
}

template <class Derived> class AppBase {
  using WindowDeleter = decltype([](SDL_Window *window) {
    SDL_DestroyWindow(window);
  });
  using InstanceDeleter = decltype([](VkInstance instance) {
    vkDestroyInstance(instance, nullptr);
  });
  class SurfaceDeleter {
    VkInstance m_instance;

  public:
    SurfaceDeleter(VkInstance instance = nullptr) : m_instance(instance) {}
    void operator()(VkSurfaceKHR surface) const {
      if (surface) {
        assert(m_instance);
        vkDestroySurfaceKHR(m_instance, surface, nullptr);
      }
    }
  };

  std::string m_app_name;
  struct SDL {
    SDL() { SDL_Init(SDL_INIT_EVERYTHING); }
    ~SDL() { SDL_Quit(); }
  } SDL;
  std::unique_ptr<SDL_Window, WindowDeleter> m_window;
  std::unique_ptr<std::remove_pointer_t<VkInstance>, InstanceDeleter>
      m_instance;
  std::unique_ptr<std::remove_pointer_t<VkSurfaceKHR>, SurfaceDeleter>
      m_surface;
  ren::vk::UniqueDevice m_device;
  ren::vk::UniqueSwapchain m_swapchain;
  ren::UniqueScene m_scene;

public:
  AppBase(std::string app_name);
  void run();

protected:
  void processEvent(const SDL_Event &e) {}
  void iterate() {}

private:
  void mainLoop();
};

template <class Derived> inline void AppBase<Derived>::run() {
  auto *impl = static_cast<Derived *>(this);
  bool quit = false;
  while (!quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        quit = true;
      }
      impl->processEvent(e);
    }

    int w, h;
    SDL_GetWindowSize(m_window.get(), &w, &h);
    m_scene->setOutputSize(w, h);
    m_swapchain->setSize(w, h);

    impl->iterate();
    m_scene->draw();
  }
  std::cout << "Done\n";
}

template <class Derived>
inline AppBase<Derived>::AppBase(std::string app_name)
    : m_app_name(std::move(app_name)) {
  std::cout << "Create SDL_Window\n";
  m_window.reset(SDL_CreateWindow(m_app_name.c_str(), SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED, 1280, 720,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN));

  std::cout << "Create VkInstance\n";
  m_instance.reset(createInstance(m_app_name.c_str()));

  std::cout << "Create VkSurface\n";
  m_surface =
      decltype(m_surface)(createSurface(m_instance.get(), m_window.get()),
                          SurfaceDeleter(m_instance.get()));

  std::cout << "Select VkPhysicalDevice\n";
  auto adapter = selectAdapter(m_instance.get());
  std::cout << "Running on " << getAdapterName(adapter) << "\n";
  std::cout << "Create ren::Device\n";
  m_device = ren::vk::Device::create(m_instance.get(), adapter);

  std::cout << "Create ren::Swapchain\n";
  m_swapchain = m_device->createSwapchain(m_surface.get());

  std::cout << "Create ren::Scene\n";
  m_scene = m_device->createScene();
  m_scene->setSwapchain(m_swapchain.get());
}
