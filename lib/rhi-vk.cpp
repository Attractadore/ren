#if REN_RHI_VULKAN
#include "core/Assert.hpp"
#include "core/Macros.hpp"
#include "core/Span.hpp"
#include "core/String.hpp"
#include "core/Vector.hpp"
#include "core/Views.hpp"
#include "rhi.hpp"

#include <fmt/format.h>
#include <volk.h>

namespace ren::rhi {

namespace {

auto load_vulkan() -> ren::Result<void, VkResult> {
  static VkResult result = [&] {
    fmt::println("vk: Load Vulkan");
    return volkInitialize();
  }();
  if (result) {
    return Failed(result);
  }
  return {};
}

const char *VK_LAYER_KHRONOS_VALIDATION_NAME = "VK_LAYER_KHRONOS_validation";

struct AdapterData {
  VkPhysicalDevice physical_device = nullptr;
  AdapterFeatures features;
  int queue_family_indices[QUEUE_FAMILY_COUNT] = {};
  VkPhysicalDeviceProperties properties;
};

constexpr usize MAX_PHYSICAL_DEVICES = 4;

struct InstanceData {
  VkInstance handle = nullptr;
  VkDebugReportCallbackEXT debug_callback = nullptr;
  StaticVector<AdapterData, MAX_PHYSICAL_DEVICES> adapters;
} instance;

constexpr std::array REQUIRED_DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME,
};

} // namespace

auto get_supported_features() -> Result<Features> {
  if (!load_vulkan()) {
    return Failed(Error::Unsupported);
  }

  VkResult result = VK_SUCCESS;

  uint32_t num_extensions = 0;
  result =
      vkEnumerateInstanceExtensionProperties(nullptr, &num_extensions, nullptr);
  if (result) {
    return Failed(Error::Unknown);
  }
  Vector<VkExtensionProperties> extensions(num_extensions);
  result = vkEnumerateInstanceExtensionProperties(nullptr, &num_extensions,
                                                  extensions.data());
  if (result) {
    return Failed(Error::Unknown);
  }

  auto is_extension_supported = [&](StringView extension) {
    auto it = std::ranges::find_if(extensions,
                                   [&](const VkExtensionProperties &props) {
                                     return extension == props.extensionName;
                                   });
    return it != extensions.end();
  };

  uint32_t num_layers = 0;
  result = vkEnumerateInstanceLayerProperties(&num_layers, nullptr);
  if (result) {
    return Failed(Error::Unknown);
  }
  Vector<VkLayerProperties> layers(num_layers);
  result = vkEnumerateInstanceLayerProperties(&num_layers, layers.data());
  if (result) {
    return Failed(Error::Unknown);
  }

  auto is_layer_supported = [&](StringView layer) {
    auto it = std::ranges::find_if(layers, [&](const VkLayerProperties &props) {
      return layer == props.layerName;
    });
    return it != layers.end();
  };

  return Features{
      .debug_names = is_extension_supported(VK_EXT_DEBUG_UTILS_EXTENSION_NAME),
      .debug_layer = is_layer_supported(VK_LAYER_KHRONOS_VALIDATION_NAME),
  };
}

auto init(const InitInfo &init_info) -> Result<void> {
  ren_assert(!instance.handle);

  if (!load_vulkan()) {
    return Failed(Error::Unsupported);
  }

  VkResult result = VK_SUCCESS;

  fmt::println("vk: Create instance");

  uint32_t num_supported_extensions = 0;
  result = vkEnumerateInstanceExtensionProperties(
      nullptr, &num_supported_extensions, nullptr);
  if (result) {
    return Failed(Error::Unknown);
  }
  Vector<VkExtensionProperties> supported_extensions(num_supported_extensions);
  result = vkEnumerateInstanceExtensionProperties(
      nullptr, &num_supported_extensions, supported_extensions.data());
  if (result) {
    return Failed(Error::Unknown);
  }

  auto is_extension_supported = [&](StringView extension) {
    auto it = std::ranges::find_if(supported_extensions,
                                   [&](const VkExtensionProperties &props) {
                                     return extension == props.extensionName;
                                   });
    return it != supported_extensions.end();
  };

  const Features &features = init_info.features;

  SmallVector<const char *> layers;
  SmallVector<const char *> extensions;
  for (const VkExtensionProperties &props : supported_extensions) {
    if (StringView(props.extensionName).ends_with("_surface")) {
      extensions.push_back(props.extensionName);
    }
  }

  if (features.debug_names) {
    fmt::println("vk: Enable debug names");
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  if (features.debug_layer) {
    fmt::println("vk: Enable validation layer");
    layers.push_back(VK_LAYER_KHRONOS_VALIDATION_NAME);
    if (is_extension_supported(VK_EXT_DEBUG_REPORT_EXTENSION_NAME)) {
      fmt::println("vk: Enable debug callback");
      extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    }
  }

  if (not layers.empty()) {
    fmt::println("vk: Enable layers:");
    for (const char *layer : layers) {
      fmt::println("{}", layer);
    }
  }
  if (not extensions.empty()) {
    fmt::println("vk: Enable extensions:");
    for (const char *extension : extensions) {
      fmt::println("{}", extension);
    }
  }

  VkApplicationInfo application_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .apiVersion = VK_API_VERSION_1_3,
  };

  VkInstanceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &application_info,
      .enabledLayerCount = static_cast<uint32_t>(layers.size()),
      .ppEnabledLayerNames = layers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };

  result = vkCreateInstance(&create_info, nullptr, &instance.handle);
  if (result) {
    exit();
    return Failed(Error::Unknown);
  }

  // TODO: replace this with volkLoadInstanceOnly when everything is migrated to
  // the RHI API.
  volkLoadInstance(instance.handle);

  if (features.debug_layer and
      is_extension_supported(VK_EXT_DEBUG_REPORT_EXTENSION_NAME)) {
    // Try to create debug callback

    VkDebugReportCallbackCreateInfoEXT cb_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
        .flags =
            VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
        .pfnCallback = [](VkDebugReportFlagsEXT flags,
                          VkDebugReportObjectTypeEXT objectType,
                          uint64_t object, size_t location, int32_t messageCode,
                          const char *pLayerPrefix, const char *pMessage,
                          void *pUserData) -> VkBool32 {
          fmt::println(stderr, "vk: {}", pMessage);
#if 0
          if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
            ren_trap();
          }
#endif
          return false;
        },
    };

    vkCreateDebugReportCallbackEXT(instance.handle, &cb_create_info, nullptr,
                                   &instance.debug_callback);
  }

  VkPhysicalDevice physical_devices[MAX_PHYSICAL_DEVICES];
  uint32_t num_physical_devices = std::size(physical_devices);
  result = vkEnumeratePhysicalDevices(instance.handle, &num_physical_devices,
                                      physical_devices);
  if (result) {
    exit();
    return Failed(Error::Unknown);
  }
  if (num_physical_devices == 0) {
    exit();
    return Failed(Error::Unsupported);
  }

  Vector<VkExtensionProperties> extension_properties;
  Vector<VkQueueFamilyProperties> queues;
  for (VkPhysicalDevice handle : Span(physical_devices, num_physical_devices)) {
    bool skip = false;

    AdapterData adapter = {.physical_device = handle};

    vkGetPhysicalDeviceProperties(handle, &adapter.properties);
    const char *device_name = adapter.properties.deviceName;

    uint32_t num_extensions;
    result = vkEnumerateDeviceExtensionProperties(handle, nullptr,
                                                  &num_extensions, nullptr);
    if (result) {
      exit();
      return Failed(Error::Unknown);
    }
    extension_properties.resize(num_extensions);
    result = vkEnumerateDeviceExtensionProperties(
        handle, nullptr, &num_extensions, extension_properties.data());
    if (result) {
      exit();
      return Failed(Error::Unknown);
    }

    auto is_extension_supported = [&](StringView extension) {
      return extension_properties.end() !=
             std::ranges::find_if(extension_properties,
                                  [&](const VkExtensionProperties &props) {
                                    return extension == props.extensionName;
                                  });
    };

    for (const char *extension : REQUIRED_DEVICE_EXTENSIONS) {
      if (not is_extension_supported(extension)) {
        fmt::println(
            "vk: Disable device {}: required extension {} is not supported",
            device_name, extension);
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }

    void *pnext = nullptr;
    auto add_features = [&](auto &features) {
      ren_assert(!features.pNext);
      features.pNext = pnext;
      pnext = &features;
    };

    VkPhysicalDeviceAntiLagFeaturesAMD amd_anti_lag_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ANTI_LAG_FEATURES_AMD,
    };

    add_features(amd_anti_lag_features);

    VkPhysicalDeviceFeatures2 vk_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = pnext,
    };

    vkGetPhysicalDeviceFeatures2(handle, &vk_features);

    adapter.features = {
        .amd_anti_lag =
            is_extension_supported(VK_AMD_ANTI_LAG_EXTENSION_NAME) and
            amd_anti_lag_features.antiLag,
    };

    std::ranges::fill(adapter.queue_family_indices, -1);

    uint32_t num_queues = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(handle, &num_queues, nullptr);
    queues.resize(num_queues);
    vkGetPhysicalDeviceQueueFamilyProperties(handle, &num_queues,
                                             queues.data());

    for (int i : range(num_queues)) {
      VkQueueFlags flags = queues[i].queueFlags;
      if (flags & VK_QUEUE_GRAPHICS_BIT) {
        adapter.queue_family_indices[(usize)QueueFamily::Graphics] = i;
        continue;
      }
      if (flags & VK_QUEUE_COMPUTE_BIT) {
        flags |= VK_QUEUE_TRANSFER_BIT;
      }
      if (flags == (flags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT))) {
        adapter.queue_family_indices[(usize)QueueFamily::Compute] = i;
        continue;
      }
    }

    if (adapter.queue_family_indices[(usize)QueueFamily::Graphics] < 0) {
      fmt::println("vk: Disable device {}: doesn't have a graphics queue",
                   device_name);
      continue;
    }

    fmt::println("vk: Found device {}", device_name);

    instance.adapters.push_back(adapter);
  }

  if (instance.adapters.empty()) {
    exit();
    return Failed(Error::Unsupported);
  }

  return {};
}

void exit() {
  if (instance.debug_callback) {
    vkDestroyDebugReportCallbackEXT(instance.handle, instance.debug_callback,
                                    nullptr);
  }
  if (instance.handle) {
    vkDestroyInstance(instance.handle, nullptr);
  }
  instance = {};
}

auto get_adapter_count() -> u32 {
  ren_assert(instance.handle);
  return instance.adapters.size();
}

auto get_adapter(u32 adapter) -> Adapter {
  ren_assert(adapter < instance.adapters.size());
  return {adapter};
}

auto get_adapter_by_preference(AdapterPreference preference) -> Adapter {
  ren_assert(instance.handle);

  VkPhysicalDeviceType preferred_type;
  if (preference == AdapterPreference::LowPower) {
    preferred_type = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
  } else if (preference == AdapterPreference::HighPerformance) {
    preferred_type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
  } else {
    ren_assert(preference == AdapterPreference::Auto);
    return {0};
  }

  for (u32 adapter : range(instance.adapters.size())) {
    if (instance.adapters[adapter].properties.deviceType == preferred_type) {
      return {adapter};
    }
  }

  if (preference == AdapterPreference::HighPerformance) {
    // Search again for an integrated GPU.
    for (u32 adapter : range(instance.adapters.size())) {
      if (instance.adapters[adapter].properties.deviceType ==
          VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        return {adapter};
      }
    }
  }

  return {0};
}

auto get_adapter_features(Adapter adapter) -> AdapterFeatures {
  ren_assert(instance.handle);
  ren_assert(adapter.index < instance.adapters.size());
  return instance.adapters[adapter.index].features;
}

auto is_queue_family_supported(Adapter adapter, QueueFamily family) -> bool {
  ren_assert(instance.handle);
  ren_assert(adapter.index < instance.adapters.size());
  return instance.adapters[adapter.index].queue_family_indices[(usize)family] >=
         0;
}

auto is_queue_family_present_supported(Adapter handle, QueueFamily family,
                                       Surface surface) -> bool {
  ren_assert(instance.handle);
  ren_assert(handle.index < instance.adapters.size());
  ren_assert(surface.handle);
  VkResult result = VK_SUCCESS;
  if (!is_queue_family_supported(handle, family)) {
    return false;
  }
  const AdapterData &adapter = instance.adapters[handle.index];
  VkBool32 supported = false;
  result = vkGetPhysicalDeviceSurfaceSupportKHR(
      adapter.physical_device, adapter.queue_family_indices[(usize)family],
      surface.handle, &supported);
  if (result) {
    return false;
  }
  return supported;
}

namespace vk {

struct DeviceData {
  VkDevice handle = nullptr;
  VmaAllocator allocator = nullptr;
  std::array<Queue, QUEUE_FAMILY_COUNT> queues;
  VolkDeviceTable vk = {};
};

} // namespace vk

auto create_device(const DeviceCreateInfo &create_info) -> Result<Device> {
  VkResult result = VK_SUCCESS;

  ren_assert(create_info.adapter.index < instance.adapters.size());
  const AdapterData &adapter = instance.adapters[create_info.adapter.index];
  VkPhysicalDevice handle = adapter.physical_device;
  ren_assert(handle);
  const AdapterFeatures &features = create_info.features;

  fmt::println("vk: Create device for {}", adapter.properties.deviceName);

  Vector<const char *> extensions = REQUIRED_DEVICE_EXTENSIONS;
  if (features.amd_anti_lag) {
    extensions.push_back(VK_AMD_ANTI_LAG_EXTENSION_NAME);
  }

  void *pnext = nullptr;
  auto add_features = [&](auto &features) {
    ren_assert(!features.pNext);
    features.pNext = pnext;
    pnext = &features;
  };

  // Add required features.
  // TODO: check that they are supported.

  pnext = nullptr;

  VkPhysicalDeviceFeatures2 vulkan10_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .features = {
          .samplerAnisotropy = true,
          .shaderInt64 = true,
          .shaderInt16 = true,
      }};

  add_features(vulkan10_features);

  VkPhysicalDeviceVulkan11Features vulkan11_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .storageBuffer16BitAccess = true,
      .shaderDrawParameters = true,
  };

  add_features(vulkan11_features);

  VkPhysicalDeviceVulkan12Features vulkan12_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .drawIndirectCount = true,
      .storageBuffer8BitAccess = true,
      .shaderInt8 = true,
      .descriptorBindingSampledImageUpdateAfterBind = true,
      .descriptorBindingStorageImageUpdateAfterBind = true,
      .descriptorBindingPartiallyBound = true,
      .samplerFilterMinmax = true,
      .scalarBlockLayout = true,
      .timelineSemaphore = true,
      .bufferDeviceAddress = true,
      .vulkanMemoryModel = true,
  };

  add_features(vulkan12_features);

  VkPhysicalDeviceVulkan13Features vulkan13_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .synchronization2 = true,
      .dynamicRendering = true,
      .maintenance4 = true,
  };

  add_features(vulkan13_features);

  VkPhysicalDeviceIndexTypeUint8FeaturesEXT uint8_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT,
      .indexTypeUint8 = true,
  };

  add_features(uint8_features);

  // Add optional features.
  // TODO: check that they are supported.

  VkPhysicalDeviceAntiLagFeaturesAMD amd_anti_lag_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ANTI_LAG_FEATURES_AMD,
      .antiLag = true,
  };

  if (features.amd_anti_lag) {
    fmt::println("vk: Enable AMD Anti-Lag");
    extensions.push_back(VK_AMD_ANTI_LAG_EXTENSION_NAME);
    add_features(amd_anti_lag_features);
  }

  float queue_priority = 1.0f;

  StaticVector<VkDeviceQueueCreateInfo, QUEUE_FAMILY_COUNT> queue_create_info;
  for (u32 i : range(QUEUE_FAMILY_COUNT)) {
    if (adapter.queue_family_indices[i] >= 0) {
      queue_create_info.push_back({
          .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
          .queueFamilyIndex = i,
          .queueCount = 1,
          .pQueuePriorities = &queue_priority,
      });
    }
  }

  if (not extensions.empty()) {
    fmt::println("vk: Enable extensions:");
    for (const char *extension : extensions) {
      fmt::println("{}", extension);
    }
  }

  VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = pnext,
      .queueCreateInfoCount = (u32)queue_create_info.size(),
      .pQueueCreateInfos = queue_create_info.data(),
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };

  DeviceData *device = new DeviceData;

  result = vkCreateDevice(handle, &device_info, nullptr, &device->handle);
  if (result == VK_ERROR_FEATURE_NOT_PRESENT) {
    return Failed(Error::FeatureNotPresent);
  } else if (result) {
    return Failed(Error::Unknown);
  }

  volkLoadDeviceTable(&device->vk, device->handle);

  for (usize i : range(QUEUE_FAMILY_COUNT)) {
    i32 qfi = adapter.queue_family_indices[i];
    if (qfi >= 0) {
      vkGetDeviceQueue(device->handle, qfi, 0, &device->queues[i].handle);
      device->queues[i].vk = &device->vk;
    }
  }

  VmaVulkanFunctions vk = {
      .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
      .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
  };

  VmaAllocatorCreateInfo allocator_info = {
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = handle,
      .device = device->handle,
      .pVulkanFunctions = &vk,
      .instance = instance.handle,
      .vulkanApiVersion = VK_API_VERSION_1_3,
  };

  result = vmaCreateAllocator(&allocator_info, &device->allocator);
  if (result) {
    destroy_device(device);
    return Failed(Error::Unknown);
  }

  return device;
}

void destroy_device(Device device) {
  if (device and device->handle) {
    vmaDestroyAllocator(device->allocator);
    device->vk.vkDestroyDevice(device->handle, nullptr);
  }
  delete device;
}

auto get_queue(Device device, QueueFamily family) -> Queue {
  ren_assert(device);
  Queue queue = device->queues[(usize)family];
  ren_assert(queue.handle);
  return queue;
};

namespace vk {

auto get_vk_instance() -> VkInstance { return instance.handle; }

auto get_vk_physical_device(Adapter adapter) -> VkPhysicalDevice {
  ren_assert(instance.handle);
  ren_assert(adapter.index < instance.adapters.size());
  return instance.adapters[adapter.index].physical_device;
}

auto get_queue_family_index(Adapter adapter, QueueFamily family) -> u32 {
  ren_assert(instance.handle);
  ren_assert(adapter.index < instance.adapters.size());
  ren_assert(is_queue_family_supported(adapter, family));
  return instance.adapters[adapter.index].queue_family_indices[(usize)family];
}

auto get_vk_device(Device device) -> VkDevice {
  ren_assert(device);
  return device->handle;
}

auto get_vma_allocator(Device device) -> VmaAllocator {
  ren_assert(device);
  return device->allocator;
}

} // namespace vk

} // namespace ren::rhi

#endif // REN_RHI_VULKAN
