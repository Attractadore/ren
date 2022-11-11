#include "RenHpp/RenVulkan.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

VkInstance CreateInstance(const char *app_name) {
  auto layers = Ren::Vk::getRequiredLayers();

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
    auto req_extensions = Ren::Vk::getRequiredExtensions();
    extensions.insert(extensions.end(), req_extensions.begin(),
                      req_extensions.end());
  }

  VkApplicationInfo application_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = app_name,
      .apiVersion = Ren::Vk::getRequiredAPIVersion(),
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

VkPhysicalDevice selectDevice(VkInstance instance, unsigned idx = 0) {
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

std::string getDeviceName(VkPhysicalDevice dev) {
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(dev, &props);
  return props.deviceName;
}

inline Ren::Device CreateDevice(VkInstance instance,
                                VkPhysicalDevice physical_device) {
  return Ren::Vk::createDevice(vkGetInstanceProcAddr, instance,
                               physical_device);
}

inline VkSurfaceKHR CreateSurface(VkInstance instance, SDL_Window *window) {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
    throw std::runtime_error{"SDL_Vulkan: Failed to create VkSurfaceKHR"};
  }
  return surface;
}

int main() {
  constexpr auto app_name = "Create device";

  std::cout << "Create SDL_Window\n";
  SDL_Init(SDL_INIT_EVERYTHING);
  using WindowDeleter =
      decltype([](SDL_Window *window) { SDL_DestroyWindow(window); });
  std::unique_ptr<SDL_Window, WindowDeleter> window(SDL_CreateWindow(
      app_name, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN));

  std::cout << "Create VkInstance\n";
  using InstanceDeleter = decltype(
      [](VkInstance instance) { vkDestroyInstance(instance, nullptr); });
  std::unique_ptr<std::remove_pointer_t<VkInstance>, InstanceDeleter> instance(
      CreateInstance(app_name));

  std::cout << "Create VkSurfaceKHR\n";
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
  std::unique_ptr<std::remove_pointer_t<VkSurfaceKHR>, SurfaceDeleter> surface(
      CreateSurface(instance.get(), window.get()),
      SurfaceDeleter(instance.get()));

  std::cout << "Select VkPhysicalDevice\n";
  auto dev = selectDevice(instance.get());
  std::cout << "Running on " << getDeviceName(dev) << "\n";
  std::cout << "Create Ren::Device\n";
  Ren::Device device = CreateDevice(instance.get(), dev);

  std::cout << "Done\n";

  SDL_Quit();
}
