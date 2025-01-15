#if REN_RHI_VULKAN
#include "core/Assert.hpp"
#include "core/Span.hpp"
#include "core/String.hpp"
#include "core/Vector.hpp"
#include "core/Views.hpp"
#include "rhi.hpp"

#include <SDL2/SDL_vulkan.h>
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <volk.h>

namespace ren::rhi {

namespace {

inline auto fail(VkResult result, String description = "") -> Failure<Error> {
  Error::Code code = Error::Unknown;
  ren_assert(result);
  switch (result) {
  default:
    break;
  case VK_ERROR_FEATURE_NOT_PRESENT:
    code = Error::FeatureNotPresent;
    break;
  case VK_ERROR_OUT_OF_DATE_KHR:
    code = Error::OutOfDate;
    break;
  case VK_INCOMPLETE:
    code = Error::Incomplete;
    break;
  }
  return Failure(Error(code, std::move(description)));
}

auto load_vulkan() -> ren::Result<void, VkResult> {
  static VkResult result = [&] {
    fmt::println("vk: Load Vulkan");
    if (SDL_Vulkan_LoadLibrary(nullptr)) {
      return VK_ERROR_UNKNOWN;
    }
    return volkInitialize();
  }();
  if (result) {
    return Failure(result);
  }
  return {};
}

const char *VK_LAYER_KHRONOS_VALIDATION_NAME = "VK_LAYER_KHRONOS_validation";

struct AdapterData {
  VkPhysicalDevice physical_device = nullptr;
  AdapterFeatures features;
  int queue_family_indices[QUEUE_FAMILY_COUNT] = {};
  VkPhysicalDeviceProperties properties;
  MemoryHeapProperties heap_properties[MEMORY_HEAP_COUNT] = {};
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
    return fail(Error::Unsupported);
  }

  VkResult result = VK_SUCCESS;

  uint32_t num_extensions = 0;
  result =
      vkEnumerateInstanceExtensionProperties(nullptr, &num_extensions, nullptr);
  if (result) {
    return fail(result);
  }
  Vector<VkExtensionProperties> extensions(num_extensions);
  result = vkEnumerateInstanceExtensionProperties(nullptr, &num_extensions,
                                                  extensions.data());
  if (result) {
    return fail(Error::Unknown);
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
    return fail(Error::Unknown);
  }
  Vector<VkLayerProperties> layers(num_layers);
  result = vkEnumerateInstanceLayerProperties(&num_layers, layers.data());
  if (result) {
    return fail(Error::Unknown);
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
    return fail(Error::Unsupported);
  }

  VkResult result = VK_SUCCESS;

  fmt::println("vk: Create instance");

  uint32_t num_supported_extensions = 0;
  result = vkEnumerateInstanceExtensionProperties(
      nullptr, &num_supported_extensions, nullptr);
  if (result) {
    return fail(Error::Unknown);
  }
  Vector<VkExtensionProperties> supported_extensions(num_supported_extensions);
  result = vkEnumerateInstanceExtensionProperties(
      nullptr, &num_supported_extensions, supported_extensions.data());
  if (result) {
    return fail(Error::Unknown);
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

  fmt::println("vk: Enable SDL2 required extensions");
  u32 num_sdl_extensions = 0;
  if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &num_sdl_extensions,
                                        nullptr)) {
    return fail(Error::Unknown);
  }
  extensions.resize(num_sdl_extensions);
  if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &num_sdl_extensions,
                                        extensions.data())) {
    return fail(Error::Unknown);
  }

  extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);

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

  if (is_extension_supported(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME)) {
    fmt::println("vk: Enable {}", VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
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
    return fail(Error::Unknown);
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
    return fail(Error::Unknown);
  }
  if (num_physical_devices == 0) {
    exit();
    return fail(Error::Unsupported);
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
      return fail(Error::Unknown);
    }
    extension_properties.resize(num_extensions);
    result = vkEnumerateDeviceExtensionProperties(
        handle, nullptr, &num_extensions, extension_properties.data());
    if (result) {
      exit();
      return fail(Error::Unknown);
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

    if (adapter.properties.deviceType ==
        VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
      adapter.heap_properties[(usize)MemoryHeap::Default] = {
          .heap_type = MemoryHeap::Default,
          .host_page_property = HostPageProperty::WriteCombined,
          .memory_pool = MemoryPool::L0,
      };
      adapter.heap_properties[(usize)MemoryHeap::Upload] = {
          .heap_type = MemoryHeap::Upload,
          .host_page_property = HostPageProperty::WriteCombined,
          .memory_pool = MemoryPool::L0,
      };
      adapter.heap_properties[(usize)MemoryHeap::DeviceUpload] = {
          .heap_type = MemoryHeap::DeviceUpload,
          .host_page_property = HostPageProperty::WriteCombined,
          .memory_pool = MemoryPool::L0,
      };
      adapter.heap_properties[(usize)MemoryHeap::Readback] = {
          .heap_type = MemoryHeap::Readback,
          .host_page_property = HostPageProperty::WriteBack,
          .memory_pool = MemoryPool::L0,
      };
    } else {
      adapter.heap_properties[(usize)MemoryHeap::Default] = {
          .heap_type = MemoryHeap::Default,
          .host_page_property = HostPageProperty::NotAvailable,
          .memory_pool = MemoryPool::L1,
      };
      adapter.heap_properties[(usize)MemoryHeap::Upload] = {
          .heap_type = MemoryHeap::Upload,
          .host_page_property = HostPageProperty::WriteCombined,
          .memory_pool = MemoryPool::L0,
      };
      adapter.heap_properties[(usize)MemoryHeap::DeviceUpload] = {
          .heap_type = MemoryHeap::DeviceUpload,
          .host_page_property = HostPageProperty::WriteCombined,
          .memory_pool = MemoryPool::L1,
      };
      adapter.heap_properties[(usize)MemoryHeap::Readback] = {
          .heap_type = MemoryHeap::Readback,
          .host_page_property = HostPageProperty::WriteBack,
          .memory_pool = MemoryPool::L0,
      };
    }

    fmt::println("vk: Found device {}", device_name);

    instance.adapters.push_back(adapter);
  }

  if (instance.adapters.empty()) {
    exit();
    return fail(Error::Unsupported);
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

auto get_memory_heap_properties(Adapter adapter, MemoryHeap heap)
    -> MemoryHeapProperties {
  return instance.adapters[adapter.index].heap_properties[(usize)heap];
}

namespace vk {

struct DeviceData {
  VkDevice handle = nullptr;
  VmaAllocator allocator = nullptr;
  Adapter adapter = {};
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
  device->adapter = create_info.adapter;

  result = vkCreateDevice(handle, &device_info, nullptr, &device->handle);
  if (result == VK_ERROR_FEATURE_NOT_PRESENT) {
    return fail(Error::FeatureNotPresent);
  } else if (result) {
    return fail(Error::Unknown);
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
    return fail(Error::Unknown);
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
}

auto map(Device device, Allocation allocation) -> void * {
  VmaAllocationInfo allocation_info;
  vmaGetAllocationInfo(device->allocator, allocation.handle, &allocation_info);
  return allocation_info.pMappedData;
}

auto create_buffer(const BufferCreateInfo &create_info) -> Result<Buffer> {
  Device device = create_info.device;
  const MemoryHeapProperties &heap_props =
      instance.adapters[device->adapter.index]
          .heap_properties[(usize)create_info.heap];

  VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = create_info.size,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
               VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
  };

  VmaAllocationCreateInfo allocation_info = {
      .usage = VMA_MEMORY_USAGE_AUTO,
  };

  if (heap_props.memory_pool == MemoryPool::L1) {
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  }

  switch (heap_props.host_page_property) {
  case HostPageProperty::NotAvailable:
    break;
  case HostPageProperty::WriteCombined: {
    allocation_info.flags =
        VMA_ALLOCATION_CREATE_MAPPED_BIT |
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  } break;
  case HostPageProperty::WriteBack: {
    allocation_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
  } break;
  }

  Buffer buffer;
  VkResult result =
      vmaCreateBuffer(device->allocator, &buffer_info, &allocation_info,
                      &buffer.handle, &buffer.allocation.handle, nullptr);
  if (result) {
    return fail(result);
  }

  return buffer;
}

void destroy_buffer(Device device, Buffer buffer) {
  vmaDestroyBuffer(device->allocator, buffer.handle, buffer.allocation.handle);
}

auto get_allocation(Device, Buffer buffer) -> Allocation {
  return buffer.allocation;
}

auto get_device_ptr(Device device, Buffer buffer) -> u64 {
  VkBufferDeviceAddressInfo address_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = buffer.handle,
  };
  return device->vk.vkGetBufferDeviceAddress(device->handle, &address_info);
}

namespace {

constexpr auto IMAGE_USAGE_MAP = [] {
  using enum ImageUsage;
  std::array<VkImageUsageFlagBits, IMAGE_USAGE_COUNT> map = {};
#define map(from, to) map[std::countr_zero((usize)from)] = to;
  map(TransferSrc, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  map(TransferDst, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  map(Sampled, VK_IMAGE_USAGE_SAMPLED_BIT);
  map(Storage, VK_IMAGE_USAGE_STORAGE_BIT);
  map(ColorAttachment, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  map(DepthAttachment, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
#undef map
  return map;
}();

auto to_vk_image_usage_flags(ImageUsageFlags flags) -> VkImageUsageFlags {
  VkImageUsageFlags vk_flags = 0;
  for (usize bit : range(IMAGE_USAGE_COUNT)) {
    auto usage = (ImageUsage)(1 << bit);
    if (flags.is_set(usage)) {
      vk_flags |= IMAGE_USAGE_MAP[bit];
    }
  }
  return vk_flags;
}

auto from_vk_image_usage_flags(VkImageUsageFlags vk_flags) -> ImageUsageFlags {
  ImageUsageFlags flags;
  for (usize bit : range(IMAGE_USAGE_COUNT)) {
    VkImageUsageFlagBits vk_usage = IMAGE_USAGE_MAP[bit];
    if (vk_flags & vk_usage) {
      auto usage = (ImageUsage)(1 << bit);
      flags |= usage;
    }
  }
  return flags;
}

} // namespace

auto create_image(const ImageCreateInfo &create_info) -> Result<Image> {
  Device device = create_info.device;

  u32 width = create_info.width;
  ren_assert(width > 0);
  u32 height = create_info.height;
  u32 depth = create_info.depth;
  VkImageType image_type = VK_IMAGE_TYPE_1D;
  if (height > 0) {
    image_type = VK_IMAGE_TYPE_2D;
  }
  if (depth > 0) {
    image_type = VK_IMAGE_TYPE_3D;
  }
  height = std::max(height, 1u);
  depth = std::max(depth, 1u);

  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = image_type,
      .format = (VkFormat)TinyImageFormat_ToVkFormat(create_info.format),
      .extent = {width, height, depth},
      .mipLevels = create_info.num_mip_levels,
      .arrayLayers = create_info.num_array_layers,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = to_vk_image_usage_flags(create_info.usage),
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO};

  Image image;
  VkResult result =
      vmaCreateImage(device->allocator, &image_info, &alloc_info, &image.handle,
                     &image.allocation.handle, nullptr);
  if (result) {
    return fail(result);
  }

  return image;
}

void destroy_image(Device device, Image image) {
  vmaDestroyImage(device->allocator, image.handle, image.allocation.handle);
}

auto get_allocation(Device, Image image) -> Allocation {
  return image.allocation;
}

namespace {

constexpr auto FILTER_MAP = [] {
  std::array<VkFilter, FILTER_COUNT> map = {};
#define map(from, to) map[(usize)from] = to
  map(Filter::Nearest, VK_FILTER_NEAREST);
  map(Filter::Linear, VK_FILTER_LINEAR);
  return map;
}();

constexpr auto SAMPLER_MIPMAP_MODE_MAP = [] {
  std::array<VkSamplerMipmapMode, SAMPLER_MIPMAP_MODE_COUNT> map = {};
  map(SamplerMipmapMode::Nearest, VK_SAMPLER_MIPMAP_MODE_NEAREST);
  map(SamplerMipmapMode::Linear, VK_SAMPLER_MIPMAP_MODE_LINEAR);
  return map;
}();

constexpr auto SAMPLER_ADDRESS_MODE_MAP = [] {
  std::array<VkSamplerAddressMode, SAMPLER_ADDRESS_MODE_COUNT> map = {};
  map(SamplerAddressMode::Repeat, VK_SAMPLER_ADDRESS_MODE_REPEAT);
  map(SamplerAddressMode::MirroredRepeat,
      VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
  map(SamplerAddressMode::ClampToEdge, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
  return map;
}();

constexpr auto SAMPLER_REDUCTION_MODE_MAP = [] {
  std::array<VkSamplerReductionMode, SAMPLER_REDUCTION_MODE_COUNT> map = {};
  map(SamplerReductionMode::WeightedAverage,
      VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE);
  map(SamplerReductionMode::Min, VK_SAMPLER_REDUCTION_MODE_MIN);
  map(SamplerReductionMode::Max, VK_SAMPLER_REDUCTION_MODE_MAX);
  return map;
}();

} // namespace

auto create_sampler(const SamplerCreateInfo &create_info) -> Result<Sampler> {
  Device device = create_info.device;
  VkSamplerReductionModeCreateInfo reduction_mode_info = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
      .reductionMode =
          SAMPLER_REDUCTION_MODE_MAP[(usize)create_info.reduction_mode],
  };
  VkSamplerCreateInfo sampler_info = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .pNext = &reduction_mode_info,
      .magFilter = FILTER_MAP[(usize)create_info.mag_filter],
      .minFilter = FILTER_MAP[(usize)create_info.min_filter],
      .mipmapMode = SAMPLER_MIPMAP_MODE_MAP[(usize)create_info.mipmap_mode],
      .addressModeU =
          SAMPLER_ADDRESS_MODE_MAP[(usize)create_info.address_mode_u],
      .addressModeV =
          SAMPLER_ADDRESS_MODE_MAP[(usize)create_info.address_mode_v],
      .addressModeW =
          SAMPLER_ADDRESS_MODE_MAP[(usize)create_info.address_mode_w],
      .anisotropyEnable = create_info.max_anisotropy > 0.0f,
      .maxAnisotropy = create_info.max_anisotropy,
      .maxLod = VK_LOD_CLAMP_NONE,
  };
  Sampler sampler;
  VkResult result = device->vk.vkCreateSampler(device->handle, &sampler_info,
                                               nullptr, &sampler.handle);
  if (result) {
    return fail(result);
  }
  return sampler;
}

void destroy_sampler(Device device, Sampler sampler) {
  device->vk.vkDestroySampler(device->handle, sampler.handle, nullptr);
}

namespace {

constexpr auto SEMAPHORE_TYPE_MAP = [] {
  std::array<VkSemaphoreType, SEMAPHORE_TYPE_COUNT> map;
  map(SemaphoreType::Binary, VK_SEMAPHORE_TYPE_BINARY);
  map(SemaphoreType::Timeline, VK_SEMAPHORE_TYPE_TIMELINE);
  return map;
}();

} // namespace

auto create_semaphore(const SemaphoreCreateInfo &create_info)
    -> Result<Semaphore> {
  Device device = create_info.device;
  VkSemaphoreTypeCreateInfo type_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = SEMAPHORE_TYPE_MAP[(usize)create_info.type],
      .initialValue = create_info.initial_value,
  };
  VkSemaphoreCreateInfo vk_create_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &type_info,
  };
  Semaphore semaphore;
  VkResult result = device->vk.vkCreateSemaphore(
      device->handle, &vk_create_info, nullptr, &semaphore.handle);
  if (result) {
    return fail(result);
  }
  return semaphore;
}

void destroy_semaphore(Device device, Semaphore semaphore) {
  device->vk.vkDestroySemaphore(device->handle, semaphore.handle, nullptr);
}

auto wait_for_semaphores(Device device,
                         TempSpan<const SemaphoreWaitInfo> wait_infos,
                         std::chrono::nanoseconds timeout)
    -> Result<WaitResult> {
  SmallVector<VkSemaphore> semaphores(wait_infos.size());
  SmallVector<u64> values(wait_infos.size());
  for (usize i : range(wait_infos.size())) {
    semaphores[i] = wait_infos[i].semaphore.handle;
    values[i] = wait_infos[i].value;
  }
  VkSemaphoreWaitInfo vk_wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = (u32)wait_infos.size(),
      .pSemaphores = semaphores.data(),
      .pValues = values.data(),
  };
  VkResult result = device->vk.vkWaitSemaphores(device->handle, &vk_wait_info,
                                                timeout.count());
  if (result == VK_SUCCESS) {
    return WaitResult::Success;
  }
  if (result == VK_TIMEOUT) {
    return WaitResult::Timeout;
  }
  return fail(result);
}

extern const u32 SDL_WINDOW_FLAGS = SDL_WINDOW_VULKAN;

auto create_surface(SDL_Window *window) -> Result<Surface> {
  Surface surface;
  if (!SDL_Vulkan_CreateSurface(window, instance.handle, &surface.handle)) {
    return fail(Error::Unknown);
  }
  return surface;
}

void destroy_surface(Surface surface) {
  vkDestroySurfaceKHR(instance.handle, surface.handle, nullptr);
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

namespace {

constexpr auto PRESENT_MODE_MAP = [] {
  using enum PresentMode;
  std::array<VkPresentModeKHR, PRESENT_MODE_COUNT> map = {};
  std::ranges::fill(map, VK_PRESENT_MODE_FIFO_KHR);
#define map(from, to) map[(usize)from] = to
  map(Immediate, VK_PRESENT_MODE_IMMEDIATE_KHR);
  map(Mailbox, VK_PRESENT_MODE_FIFO_KHR);
  map(Fifo, VK_PRESENT_MODE_FIFO_KHR);
  map(FifoRelaxed, VK_PRESENT_MODE_FIFO_RELAXED_KHR);
#undef map
  return map;
}();

auto to_vk_present_mode(PresentMode present_mode) -> VkPresentModeKHR {
  return PRESENT_MODE_MAP[(usize)present_mode];
}

auto from_vk_present_mode(VkPresentModeKHR present_mode) -> PresentMode {
  auto it = std::ranges::find(PRESENT_MODE_MAP, present_mode);
  ren_assert(it != PRESENT_MODE_MAP.end());
  return (PresentMode)(it - PRESENT_MODE_MAP.begin());
}

} // namespace

auto get_surface_present_modes(Adapter adapter, Surface surface,
                               u32 *num_present_modes,
                               PresentMode *present_modes) -> Result<void> {
  VkPresentModeKHR vk_present_modes[PRESENT_MODE_COUNT];
  u32 num_vk_present_modes = std::size(vk_present_modes);
  VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(
      instance.adapters[adapter.index].physical_device, surface.handle,
      &num_vk_present_modes, vk_present_modes);
  if (result) {
    ren_assert(result != VK_INCOMPLETE);
    return fail(Error::Unknown);
  }
  if (present_modes) {
    for (usize i : range(std::min(*num_present_modes, num_vk_present_modes))) {
      present_modes[i] = from_vk_present_mode(vk_present_modes[i]);
    }
    if (*num_present_modes < num_vk_present_modes) {
      return fail(Error::Incomplete);
    }
  }
  *num_present_modes = num_vk_present_modes;
  return {};
}

auto get_surface_formats(Adapter adapter, Surface surface, u32 *num_formats,
                         TinyImageFormat *formats) -> Result<void> {
  VkResult result = VK_SUCCESS;
  VkPhysicalDevice physical_device =
      instance.adapters[adapter.index].physical_device;
  u32 num_vk_formats = 0;
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface.handle,
                                                &num_vk_formats, nullptr);
  if (result) {
    return fail(Error::Unknown);
  }
  Vector<VkSurfaceFormatKHR> vk_formats(num_vk_formats);
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(
      physical_device, surface.handle, &num_vk_formats, vk_formats.data());
  if (result) {
    return fail(Error::Unknown);
  }
  vk_formats.erase_if([](const VkSurfaceFormatKHR &vk_format) {
    return !TinyImageFormat_FromVkFormat(
        (TinyImageFormat_VkFormat)vk_format.format);
  });
  num_vk_formats = vk_formats.size();

  if (formats) {
    for (usize i : range(std::min(*num_formats, num_vk_formats))) {
      formats[i] = TinyImageFormat_FromVkFormat(
          (TinyImageFormat_VkFormat)vk_formats[i].format);
    }
    if (*num_formats < num_vk_formats) {
      return fail(Error::Incomplete);
    }
  }
  *num_formats = num_vk_formats;
  return {};
}

auto get_surface_supported_image_usage(Adapter adapter, Surface surface)
    -> Result<Flags<ImageUsage>> {
  VkPhysicalDevice physical_device =
      instance.adapters[adapter.index].physical_device;
  VkSurfaceCapabilitiesKHR capabilities;
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface.handle,
                                                &capabilities)) {
    return fail(Error::Unknown);
  }
  return from_vk_image_usage_flags(capabilities.supportedUsageFlags);
}

namespace vk {

constexpr u32 SWAP_CHAIN_IMAGE_NOT_ACQUIRED = -1;

struct SwapChainData {
  Device device = nullptr;
  VkQueue queue = nullptr;
  VkSurfaceKHR surface = nullptr;
  VkSwapchainKHR handle = nullptr;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  glm::uvec2 size = {};
  u32 num_images = 0;
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  u32 image = SWAP_CHAIN_IMAGE_NOT_ACQUIRED;
};

} // namespace vk

namespace {

auto adjust_swap_chain_image_count(u32 num_images,
                                   const VkSurfaceCapabilitiesKHR &capabilities)
    -> u32 {
  num_images = std::clamp(num_images, capabilities.minImageCount,
                          MAX_SWAP_CHAIN_IMAGE_COUNT);
  if (capabilities.maxImageCount) {
    num_images = std::min(num_images, capabilities.maxImageCount);
  }
  return num_images;
}

auto adjust_swap_chain_size(glm::uvec2 size,
                            const VkSurfaceCapabilitiesKHR &capabilities)
    -> glm::uvec2 {
  glm::uvec2 current_size = {capabilities.currentExtent.width,
                             capabilities.currentExtent.height};
  glm::uvec2 min_size = {capabilities.minImageExtent.width,
                         capabilities.minImageExtent.height};
  glm::uvec2 max_size = {capabilities.maxImageExtent.width,
                         capabilities.maxImageExtent.height};
  if (glm::all(glm::notEqual(current_size, glm::uvec2(-1)))) {
    size = current_size;
  }
  size = glm::max(size, min_size);
  size = glm::min(size, max_size);
  return size;
}

auto select_swap_chain_composite_alpha(
    const VkSurfaceCapabilitiesKHR &capabilities)
    -> VkCompositeAlphaFlagBitsKHR {
  constexpr std::array PREFERRED_ORDER = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
  };
  for (VkCompositeAlphaFlagBitsKHR composite_alpha : PREFERRED_ORDER) {
    if (capabilities.supportedCompositeAlpha & composite_alpha) {
      return composite_alpha;
    }
  }
  std::unreachable();
}

} // namespace

namespace {

auto recreate_swap_chain(SwapChain swap_chain, glm::uvec2 size, u32 num_images,
                         VkPresentModeKHR present_mode) -> Result<void> {
  Device device = swap_chain->device;
  const AdapterData &adapter = instance.adapters[device->adapter.index];
  VkSurfacePresentModeEXT present_mode_info = {
      .sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_EXT,
      .presentMode = present_mode,
  };
  VkPhysicalDeviceSurfaceInfo2KHR surface_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
      .pNext = &present_mode_info,
      .surface = swap_chain->surface,
  };
  VkSurfaceCapabilities2KHR capabilities2 = {
      .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
  };
  if (vkGetPhysicalDeviceSurfaceCapabilities2KHR(
          adapter.physical_device, &surface_info, &capabilities2)) {
    return fail(Error::Unknown);
  }
  const VkSurfaceCapabilitiesKHR capabilities =
      capabilities2.surfaceCapabilities;
  size = adjust_swap_chain_size(size, capabilities),
  num_images = adjust_swap_chain_image_count(num_images, capabilities);
  VkSwapchainKHR old_swap_chain = swap_chain->handle;
  VkSwapchainCreateInfoKHR vk_create_info = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = swap_chain->surface,
      .minImageCount = num_images,
      .imageFormat = swap_chain->format,
      .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
      .imageExtent = {size.x, size.y},
      .imageArrayLayers = 1,
      .imageUsage = swap_chain->usage,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = capabilities.currentTransform,
      .compositeAlpha = select_swap_chain_composite_alpha(capabilities),
      .presentMode = present_mode,
      .clipped = true,
      .oldSwapchain = old_swap_chain,
  };
  if (device->vk.vkCreateSwapchainKHR(device->handle, &vk_create_info, nullptr,
                                      &swap_chain->handle)) {
    return fail(Error::Unknown);
  }
  device->vk.vkDestroySwapchainKHR(device->handle, old_swap_chain, nullptr);
  swap_chain->size = size;
  swap_chain->num_images = num_images;
  swap_chain->present_mode = present_mode;
  return {};
}

} // namespace

auto create_swap_chain(const SwapChainCreateInfo &create_info)
    -> Result<SwapChain> {
  SwapChain swap_chain = new (SwapChainData){
      .device = create_info.device,
      .queue = create_info.queue.handle,
      .surface = create_info.surface.handle,
      .format = (VkFormat)TinyImageFormat_ToVkFormat(create_info.format),
      .usage = to_vk_image_usage_flags(create_info.usage),
      .size = {create_info.width, create_info.height},
      .num_images = create_info.num_images,
      .present_mode = to_vk_present_mode(create_info.present_mode),
  };
  Result<void> result =
      recreate_swap_chain(swap_chain, swap_chain->size, swap_chain->num_images,
                          swap_chain->present_mode);
  if (!result) {
    destroy_swap_chain(swap_chain);
    return Failure(result.error());
  }
  return swap_chain;
}

void destroy_swap_chain(SwapChain swap_chain) {
  if (swap_chain) {
    swap_chain->device->vk.vkDestroySwapchainKHR(swap_chain->device->handle,
                                                 swap_chain->handle, nullptr);
    delete swap_chain;
  }
}

auto get_swap_chain_size(SwapChain swap_chain) -> glm::uvec2 {
  return swap_chain->size;
}

auto get_swap_chain_images(SwapChain swap_chain, u32 *num_images, Image *images)
    -> Result<void> {
  VkImage vk_images[MAX_SWAP_CHAIN_IMAGE_COUNT];
  u32 num_vk_images = std::size(vk_images);
  VkResult result = swap_chain->device->vk.vkGetSwapchainImagesKHR(
      swap_chain->device->handle, swap_chain->handle, &num_vk_images,
      vk_images);
  if (result) {
    ren_assert(result != VK_INCOMPLETE);
    return fail(Error::Unknown);
  }
  if (images) {
    for (usize i : range(std::min(*num_images, num_vk_images))) {
      images[i] = {.handle = vk_images[i]};
    }
    if (*num_images < num_vk_images) {
      return fail(Error::Incomplete);
    }
  }
  *num_images = num_vk_images;
  return {};
}

auto resize_swap_chain(SwapChain swap_chain, glm::uvec2 size, u32 num_images)
    -> Result<void> {
  return recreate_swap_chain(swap_chain, size, num_images,
                             swap_chain->present_mode);
}

auto set_present_mode(SwapChain swap_chain, PresentMode present_mode)
    -> Result<void> {
  return recreate_swap_chain(swap_chain, swap_chain->size,
                             swap_chain->num_images,
                             to_vk_present_mode(present_mode));
}

auto acquire_image(SwapChain swap_chain, Semaphore semaphore) -> Result<u32> {
  ren_assert(swap_chain);
  ren_assert(swap_chain->image == vk::SWAP_CHAIN_IMAGE_NOT_ACQUIRED);
  Device device = swap_chain->device;
  VkResult result = device->vk.vkAcquireNextImageKHR(
      device->handle, swap_chain->handle, UINT64_MAX, semaphore.handle, nullptr,
      &swap_chain->image);
  if (result and result != VK_SUBOPTIMAL_KHR) {
    return fail(result);
  }
  return swap_chain->image;
}

auto present(SwapChain swap_chain, Semaphore semaphore) -> Result<void> {
  ren_assert(swap_chain);
  ren_assert(swap_chain->image != vk::SWAP_CHAIN_IMAGE_NOT_ACQUIRED);
  Device device = swap_chain->device;
  VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &semaphore.handle,
      .swapchainCount = 1,
      .pSwapchains = &swap_chain->handle,
      .pImageIndices = &swap_chain->image,
  };
  VkResult result =
      device->vk.vkQueuePresentKHR(swap_chain->queue, &present_info);
  swap_chain->image = vk::SWAP_CHAIN_IMAGE_NOT_ACQUIRED;
  if (result and result != VK_SUBOPTIMAL_KHR) {
    return fail(result);
  }
  return {};
}

namespace vk {

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
