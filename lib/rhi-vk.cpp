#if REN_RHI_VULKAN
#include "core/Assert.hpp"
#include "core/Span.hpp"
#include "core/String.hpp"
#include "core/Vector.hpp"
#include "core/Views.hpp"
#include "glsl/Texture.h"
#include "rhi.hpp"

#include <SDL2/SDL_vulkan.h>
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <utility>
#include <volk.h>

#define map(from, to) map[(usize)from] = to
#define map_bit(from, to) map[std::countr_zero((usize)from)] = to;

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

const char *VK_LAYER_KHRONOS_VALIDATION_NAME = "VK_LAYER_KHRONOS_validation";

constexpr u32 QUEUE_FAMILY_UNAVAILABLE = -1;

struct AdapterData {
  VkPhysicalDevice physical_device = nullptr;
  AdapterFeatures features;
  u32 queue_family_indices[QUEUE_FAMILY_COUNT] = {};
  VkPhysicalDeviceProperties properties;
  MemoryHeapProperties heap_properties[MEMORY_HEAP_COUNT] = {};
};

constexpr usize MAX_PHYSICAL_DEVICES = 4;

struct InstanceData {
  VkInstance handle = nullptr;
  VkDebugReportCallbackEXT debug_callback = nullptr;
  StaticVector<AdapterData, MAX_PHYSICAL_DEVICES> adapters;
  bool headless = false;
} g_instance;

constexpr std::array REQUIRED_DEVICE_EXTENSIONS = {
    VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME,
    VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME,
};

constexpr std::array REQUIRED_NON_HEADLESS_DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

} // namespace

namespace vk {

struct DeviceData {
  VkDevice handle = nullptr;
  VmaAllocator allocator = nullptr;
  Adapter adapter = {};
  std::array<Queue, QUEUE_FAMILY_COUNT> queues;
  VkDescriptorSetLayout descriptor_set_layout = nullptr;
  VkPipelineLayout pipeline_layout = nullptr;
  VkDescriptorPool descriptor_pool = nullptr;
  VkDescriptorSet descriptor_heap = nullptr;
  VolkDeviceTable vk = {};
};

} // namespace vk

namespace {

auto set_debug_name(Device device, VkObjectType type, void *object,
                    const char *name) -> Result<void> {
  if (vkSetDebugUtilsObjectNameEXT) {
    VkDebugUtilsObjectNameInfoEXT name_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = (u64)object,
        .pObjectName = name,
    };
    VkResult result = vkSetDebugUtilsObjectNameEXT(device->handle, &name_info);
    if (result) {
      return fail(result);
    }
  }
  return {};
}

} // namespace

auto load(bool headless) -> Result<void> {
  static VkResult result = [&] {
    fmt::println("vk: Load Vulkan");
    if (not headless) {
      if (SDL_Vulkan_LoadLibrary(nullptr)) {
        return VK_ERROR_UNKNOWN;
      }
    }
    return volkInitialize();
  }();
  if (result) {
    return fail(result);
  }
  g_instance.headless = headless;
  return {};
}

auto get_supported_features() -> Result<Features> {
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
  ren_assert(!g_instance.handle);

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

  if (not g_instance.headless) {
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

    if (is_extension_supported(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME)) {
      fmt::println("vk: Enable {}",
                   VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
      extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
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

  result = vkCreateInstance(&create_info, nullptr, &g_instance.handle);
  if (result) {
    exit();
    return fail(Error::Unknown);
  }

  // TODO: replace this with volkLoadInstanceOnly when everything is migrated to
  // the RHI API.
  volkLoadInstance(g_instance.handle);

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

    vkCreateDebugReportCallbackEXT(g_instance.handle, &cb_create_info, nullptr,
                                   &g_instance.debug_callback);
  }

  VkPhysicalDevice physical_devices[MAX_PHYSICAL_DEVICES];
  uint32_t num_physical_devices = std::size(physical_devices);
  result = vkEnumeratePhysicalDevices(g_instance.handle, &num_physical_devices,
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
    if (not g_instance.headless) {
      for (const char *extension : REQUIRED_NON_HEADLESS_DEVICE_EXTENSIONS) {
        if (not is_extension_supported(extension)) {
          fmt::println(
              "vk: Disable device {}: required extension {} is not supported",
              device_name, extension);
          skip = true;
          break;
        }
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

    std::ranges::fill(adapter.queue_family_indices, QUEUE_FAMILY_UNAVAILABLE);

    uint32_t num_queues = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(handle, &num_queues, nullptr);
    queues.resize(num_queues);
    vkGetPhysicalDeviceQueueFamilyProperties(handle, &num_queues,
                                             queues.data());

    for (int i : range(num_queues)) {
      VkQueueFlags queue_flags = queues[i].queueFlags;
      VkQueueFlags graphics_queue_flags =
          VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
      if ((queue_flags & (graphics_queue_flags)) == graphics_queue_flags) {
        fmt::println("vk: {}: found graphics queue", device_name);
        adapter.queue_family_indices[(usize)QueueFamily::Graphics] = i;
        continue;
      }
      if (queue_flags & VK_QUEUE_COMPUTE_BIT) {
        fmt::println("vk: {}: found compute queue", device_name);
        adapter.queue_family_indices[(usize)QueueFamily::Compute] = i;
        continue;
      }
      if ((queue_flags & VK_QUEUE_TRANSFER_BIT) and
          adapter.properties.deviceType ==
              VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        fmt::println("vk: {}: found transfer queue", device_name);
        adapter.queue_family_indices[(usize)QueueFamily::Transfer] = i;
        continue;
      }
    }

    if (adapter.queue_family_indices[(usize)QueueFamily::Graphics] ==
        QUEUE_FAMILY_UNAVAILABLE) {
      fmt::println("vk: {}: disable, doesn't have a graphics queue",
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

    fmt::println("vk: {}: enable", device_name);

    g_instance.adapters.push_back(adapter);
  }

  if (g_instance.adapters.empty()) {
    exit();
    return fail(Error::Unsupported);
  }

  return {};
}

void exit() {
  if (g_instance.debug_callback) {
    vkDestroyDebugReportCallbackEXT(g_instance.handle,
                                    g_instance.debug_callback, nullptr);
  }
  if (g_instance.handle) {
    vkDestroyInstance(g_instance.handle, nullptr);
  }
  g_instance = {};
}

auto get_adapter_count() -> u32 {
  ren_assert(g_instance.handle);
  return g_instance.adapters.size();
}

auto get_adapter(u32 adapter) -> Adapter {
  ren_assert(adapter < g_instance.adapters.size());
  return {adapter};
}

auto get_adapter_by_preference(AdapterPreference preference) -> Adapter {
  ren_assert(g_instance.handle);

  VkPhysicalDeviceType preferred_type;
  if (preference == AdapterPreference::LowPower) {
    preferred_type = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
  } else if (preference == AdapterPreference::HighPerformance) {
    preferred_type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
  } else {
    ren_assert(preference == AdapterPreference::Auto);
    return {0};
  }

  for (u32 adapter : range(g_instance.adapters.size())) {
    if (g_instance.adapters[adapter].properties.deviceType == preferred_type) {
      return {adapter};
    }
  }

  if (preference == AdapterPreference::HighPerformance) {
    // Search again for an integrated GPU.
    for (u32 adapter : range(g_instance.adapters.size())) {
      if (g_instance.adapters[adapter].properties.deviceType ==
          VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        return {adapter};
      }
    }
  }

  return {0};
}

auto get_adapter_features(Adapter adapter) -> AdapterFeatures {
  ren_assert(g_instance.handle);
  ren_assert(adapter.index < g_instance.adapters.size());
  return g_instance.adapters[adapter.index].features;
}

auto is_queue_family_supported(Adapter adapter, QueueFamily family) -> bool {
  ren_assert(g_instance.handle);
  ren_assert(adapter.index < g_instance.adapters.size());
  return g_instance.adapters[adapter.index]
             .queue_family_indices[(usize)family] != QUEUE_FAMILY_UNAVAILABLE;
}

auto get_memory_heap_properties(Adapter adapter, MemoryHeap heap)
    -> MemoryHeapProperties {
  return g_instance.adapters[adapter.index].heap_properties[(usize)heap];
}

auto create_device(const DeviceCreateInfo &create_info) -> Result<Device> {
  VkResult result = VK_SUCCESS;

  ren_assert(create_info.adapter.index < g_instance.adapters.size());
  const AdapterData &adapter = g_instance.adapters[create_info.adapter.index];
  VkPhysicalDevice handle = adapter.physical_device;
  ren_assert(handle);
  const AdapterFeatures &features = create_info.features;

  fmt::println("vk: Create device for {}", adapter.properties.deviceName);

  Vector<const char *> extensions = REQUIRED_DEVICE_EXTENSIONS;
  if (not g_instance.headless) {
    extensions.append(REQUIRED_NON_HEADLESS_DEVICE_EXTENSIONS);
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
          .textureCompressionBC = true,
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
      .descriptorBindingVariableDescriptorCount = true,
      .runtimeDescriptorArray = true,
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

  VkPhysicalDeviceComputeShaderDerivativesFeaturesNV
      compute_shader_derivatives_features = {
          .sType =
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV,
          .computeDerivativeGroupLinear = true,
      };
  add_features(compute_shader_derivatives_features);

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
    if (adapter.queue_family_indices[i] != QUEUE_FAMILY_UNAVAILABLE) {
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
    u32 qfi = adapter.queue_family_indices[i];
    if (qfi != QUEUE_FAMILY_UNAVAILABLE) {
      device->vk.vkGetDeviceQueue(device->handle, qfi, 0,
                                  &device->queues[i].handle);
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
      .instance = g_instance.handle,
      .vulkanApiVersion = VK_API_VERSION_1_3,
  };

  result = vmaCreateAllocator(&allocator_info, &device->allocator);
  if (result) {
    destroy_device(device);
    return fail(Error::Unknown);
  }

  {
    VkDescriptorBindingFlags flags[4] = {};
    std::ranges::fill(flags, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                 VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = std::size(flags),
        .pBindingFlags = flags,
    };

    VkDescriptorSetLayoutBinding bindings[4] = {};
    bindings[glsl::SAMPLER_SLOT] = {
        .binding = glsl::SAMPLER_SLOT,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .descriptorCount = glsl::MAX_NUM_SAMPLERS,
        .stageFlags =
            VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
    };
    bindings[glsl::SRV_SLOT] = {
        .binding = glsl::SRV_SLOT,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .descriptorCount = glsl::MAX_NUM_RESOURCES,
        .stageFlags =
            VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
    };
    bindings[glsl::CIS_SLOT] = {
        .binding = glsl::CIS_SLOT,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = glsl::MAX_NUM_RESOURCES,
        .stageFlags =
            VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
    };
    bindings[glsl::UAV_SLOT] = {
        .binding = glsl::UAV_SLOT,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = glsl::MAX_NUM_RESOURCES,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    VkDescriptorSetLayoutCreateInfo set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flags_info,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = std::size(bindings),
        .pBindings = bindings,
    };
    result = device->vk.vkCreateDescriptorSetLayout(
        device->handle, &set_layout_info, nullptr,
        &device->descriptor_set_layout);
    if (result) {
      destroy_device(device);
      return fail(result);
    }

    VkPushConstantRange push_constants = {
        .stageFlags = VK_SHADER_STAGE_ALL,
        .size = MAX_PUSH_CONSTANTS_SIZE,
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &device->descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constants,
    };
    result = device->vk.vkCreatePipelineLayout(
        device->handle, &layout_info, nullptr, &device->pipeline_layout);
    if (result) {
      destroy_device(device);
      return fail(result);
    }
  }

  {
    VkDescriptorPoolSize pool_sizes[4] = {};
    pool_sizes[glsl::SAMPLER_SLOT] = {
        .type = VK_DESCRIPTOR_TYPE_SAMPLER,
        .descriptorCount = glsl::MAX_NUM_SAMPLERS,
    };
    pool_sizes[glsl::SRV_SLOT] = {
        .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .descriptorCount = glsl::MAX_NUM_RESOURCES,
    };
    pool_sizes[glsl::CIS_SLOT] = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = glsl::MAX_NUM_RESOURCES,
    };
    pool_sizes[glsl::UAV_SLOT] = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = glsl::MAX_NUM_RESOURCES,
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = std::size(pool_sizes),
        .pPoolSizes = pool_sizes,
    };
    result = device->vk.vkCreateDescriptorPool(
        device->handle, &pool_info, nullptr, &device->descriptor_pool);
    if (result) {
      destroy_device(device);
      return fail(result);
    }

    VkDescriptorSetAllocateInfo set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = device->descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &device->descriptor_set_layout,
    };
    result = device->vk.vkAllocateDescriptorSets(device->handle, &set_info,
                                                 &device->descriptor_heap);
    if (result) {
      destroy_device(device);
      return fail(result);
    }
  }

  return device;
}

void destroy_device(Device device) {
  if (device and device->handle) {
    VkDevice handle = device->handle;
    const VolkDeviceTable &vk = device->vk;
    vk.vkDestroyDescriptorSetLayout(handle, device->descriptor_set_layout,
                                    nullptr);
    vk.vkDestroyPipelineLayout(handle, device->pipeline_layout, nullptr);
    vk.vkDestroyDescriptorPool(handle, device->descriptor_pool, nullptr);
    vmaDestroyAllocator(device->allocator);
    vk.vkDestroyDevice(handle, nullptr);
  }
  delete device;
}

auto device_wait_idle(Device device) -> Result<void> {
  VkResult result = device->vk.vkDeviceWaitIdle(device->handle);
  if (result < 0) {
    return fail(result);
  }
  return {};
}

namespace {

auto get_adapter(Adapter adapter) -> const AdapterData & {
  return g_instance.adapters[adapter.index];
}

auto get_adapter(Device device) -> const AdapterData & {
  return get_adapter(device->adapter);
}

} // namespace

auto get_queue(Device device, QueueFamily family) -> Queue {
  ren_assert(device);
  Queue queue = device->queues[(usize)family];
  ren_assert(queue.handle);
  return queue;
}

auto queue_submit(Queue queue, TempSpan<const rhi::CommandBuffer> cmd_buffers,
                  TempSpan<const rhi::SemaphoreState> wait_semaphores,
                  TempSpan<const rhi::SemaphoreState> signal_semaphores)
    -> Result<void> {
  SmallVector<VkCommandBufferSubmitInfo> command_buffer_infos(
      cmd_buffers.size());
  for (usize i : range(cmd_buffers.size())) {
    command_buffer_infos[i] = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd_buffers[i].handle,
    };
  }
  SmallVector<VkSemaphoreSubmitInfo> semaphore_submit_infos(
      wait_semaphores.size() + signal_semaphores.size());
  for (usize i : range(wait_semaphores.size())) {
    semaphore_submit_infos[i] = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = wait_semaphores[i].semaphore.handle,
        .value = wait_semaphores[i].value,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
  }
  for (usize i : range(signal_semaphores.size())) {
    semaphore_submit_infos[wait_semaphores.size() + i] = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = signal_semaphores[i].semaphore.handle,
        .value = signal_semaphores[i].value,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
  }
  VkSubmitInfo2 submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .waitSemaphoreInfoCount = (u32)wait_semaphores.size(),
      .pWaitSemaphoreInfos = semaphore_submit_infos.data(),
      .commandBufferInfoCount = (u32)cmd_buffers.size(),
      .pCommandBufferInfos = command_buffer_infos.data(),
      .signalSemaphoreInfoCount = (u32)signal_semaphores.size(),
      .pSignalSemaphoreInfos =
          semaphore_submit_infos.data() + wait_semaphores.size(),
  };
  VkResult result =
      queue.vk->vkQueueSubmit2(queue.handle, 1, &submit_info, nullptr);
  if (result) {
    return fail(result);
  }
  return {};
}

auto queue_wait_idle(Queue queue) -> Result<void> {
  VkResult result = queue.vk->vkQueueWaitIdle(queue.handle);
  if (result) {
    return fail(result);
  }
  return {};
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

auto create_buffer(const BufferCreateInfo &create_info) -> Result<Buffer> {
  Device device = create_info.device;
  const MemoryHeapProperties &heap_props =
      g_instance.adapters[device->adapter.index]
          .heap_properties[(usize)create_info.heap];

  StaticVector<u32, QUEUE_FAMILY_COUNT> queue_family_indices(
      get_adapter(device).queue_family_indices);
  queue_family_indices.erase(QUEUE_FAMILY_UNAVAILABLE);

  VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = create_info.size,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
               VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      .sharingMode = queue_family_indices.size() > 1
                         ? VK_SHARING_MODE_CONCURRENT
                         : VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = (u32)queue_family_indices.size(),
      .pQueueFamilyIndices = queue_family_indices.data(),
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

auto set_debug_name(Device device, Buffer buffer, const char *name)
    -> Result<void> {
  return set_debug_name(device, VK_OBJECT_TYPE_BUFFER, buffer.handle, name);
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
  map_bit(TransferSrc, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  map_bit(TransferDst, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  map_bit(ShaderResource, VK_IMAGE_USAGE_SAMPLED_BIT);
  map_bit(UnorderedAccess, VK_IMAGE_USAGE_STORAGE_BIT);
  map_bit(RenderTarget, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  map_bit(DepthStencilTarget, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
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

  ImageUsageFlags usage = create_info.usage;

  StaticVector<u32, QUEUE_FAMILY_COUNT> queue_family_indices(
      get_adapter(device).queue_family_indices);
  if (!usage.is_any_set(ImageUsage::TransferSrc | ImageUsage::TransferDst)) {
    queue_family_indices[(usize)QueueFamily::Transfer] =
        QUEUE_FAMILY_UNAVAILABLE;
  }
  if (!usage.is_any_set(ImageUsage::ShaderResource |
                        ImageUsage::UnorderedAccess)) {
    queue_family_indices[(usize)QueueFamily::Compute] =
        QUEUE_FAMILY_UNAVAILABLE;
  }
  queue_family_indices.erase(QUEUE_FAMILY_UNAVAILABLE);
  ren_assert(not queue_family_indices.empty());

  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .flags = create_info.cube_map ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
                                    : VkImageCreateFlags(0),
      .imageType = image_type,
      .format = (VkFormat)TinyImageFormat_ToVkFormat(create_info.format),
      .extent = {width, height, depth},
      .mipLevels = create_info.num_mips,
      .arrayLayers = create_info.num_layers * (create_info.cube_map ? 6 : 1),
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = to_vk_image_usage_flags(create_info.usage),
      .sharingMode = queue_family_indices.size() > 1
                         ? VK_SHARING_MODE_CONCURRENT
                         : VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = (u32)queue_family_indices.size(),
      .pQueueFamilyIndices = queue_family_indices.data(),
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

auto set_debug_name(Device device, Image image, const char *name)
    -> Result<void> {
  return set_debug_name(device, VK_OBJECT_TYPE_IMAGE, image.handle, name);
}

auto get_allocation(Device, Image image) -> Allocation {
  return image.allocation;
}

namespace {

constexpr auto VIEW_TYPE_MAP = [] {
  std::array<VkImageViewType, IMAGE_VIEW_DIMENSION_COUNT> map = {};
  map(ImageViewDimension::e1D, VK_IMAGE_VIEW_TYPE_1D);
  map(ImageViewDimension::e2D, VK_IMAGE_VIEW_TYPE_2D);
  map(ImageViewDimension::eCube, VK_IMAGE_VIEW_TYPE_CUBE);
  map(ImageViewDimension::e3D, VK_IMAGE_VIEW_TYPE_3D);
  return map;
}();

constexpr auto COMPONENT_SWIZZLE_MAP = [] {
  std::array<VkComponentSwizzle, COMPONENT_SWIZZLE_COUNT> map = {};
  map(ComponentSwizzle::Identity, VK_COMPONENT_SWIZZLE_IDENTITY);
  map(ComponentSwizzle::Zero, VK_COMPONENT_SWIZZLE_ZERO);
  map(ComponentSwizzle::One, VK_COMPONENT_SWIZZLE_ONE);
  map(ComponentSwizzle::R, VK_COMPONENT_SWIZZLE_R);
  map(ComponentSwizzle::G, VK_COMPONENT_SWIZZLE_G);
  map(ComponentSwizzle::B, VK_COMPONENT_SWIZZLE_B);
  map(ComponentSwizzle::A, VK_COMPONENT_SWIZZLE_A);
  return map;
}();

auto toVkFormat(TinyImageFormat format) -> VkFormat {
  return (VkFormat)TinyImageFormat_ToVkFormat(format);
}

auto get_format_aspect_mask(TinyImageFormat format) -> VkImageAspectFlags {
  if (TinyImageFormat_IsDepthOnly(format) or
      TinyImageFormat_IsDepthAndStencil(format)) {
    return VK_IMAGE_ASPECT_DEPTH_BIT;
  }
  return VK_IMAGE_ASPECT_COLOR_BIT;
}

} // namespace

auto create_image_view(Device device, const ImageViewCreateInfo &create_info)
    -> Result<ImageView> {
  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = create_info.image.handle,
      .viewType = VIEW_TYPE_MAP[(usize)create_info.dimension],
      .format = toVkFormat(create_info.format),
      .components =
          {
              .r = COMPONENT_SWIZZLE_MAP[(usize)create_info.components.r],
              .g = COMPONENT_SWIZZLE_MAP[(usize)create_info.components.g],
              .b = COMPONENT_SWIZZLE_MAP[(usize)create_info.components.b],
              .a = COMPONENT_SWIZZLE_MAP[(usize)create_info.components.a],
          },
      .subresourceRange =
          {
              .aspectMask = get_format_aspect_mask(create_info.format),
              .baseMipLevel = create_info.base_mip,
              .levelCount = create_info.num_mips,
              .baseArrayLayer = create_info.base_layer,
              .layerCount =
                  create_info.dimension == ImageViewDimension::eCube ? 6u : 1u,
          },
  };
  ImageView view;
  VkResult result = device->vk.vkCreateImageView(device->handle, &view_info,
                                                 nullptr, &view.handle);
  if (result) {
    return fail(result);
  }
  return view;
}

void destroy_image_view(Device device, ImageView view) {
  device->vk.vkDestroyImageView(device->handle, view.handle, nullptr);
}

namespace {

constexpr auto FILTER_MAP = [] {
  std::array<VkFilter, FILTER_COUNT> map = {};
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

void write_sampler_descriptor_heap(Device device,
                                   TempSpan<const Sampler> samplers,
                                   u32 index) {
  SmallVector<VkDescriptorImageInfo> image_info(samplers.size());
  for (usize i : range(samplers.size())) {
    image_info[i] = {.sampler = samplers[i].handle};
  }
  VkWriteDescriptorSet write_info = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = device->descriptor_heap,
      .dstBinding = glsl::SAMPLER_SLOT,
      .dstArrayElement = index,
      .descriptorCount = (u32)image_info.size(),
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
      .pImageInfo = image_info.data(),
  };
  device->vk.vkUpdateDescriptorSets(device->handle, 1, &write_info, 0, nullptr);
}

void write_srv_descriptor_heap(Device device, TempSpan<const ImageView> srvs,
                               u32 index) {
  SmallVector<VkDescriptorImageInfo> image_info(srvs.size());
  for (usize i : range(srvs.size())) {
    image_info[i] = {
        .imageView = srvs[i].handle,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
  }
  VkWriteDescriptorSet write_info = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = device->descriptor_heap,
      .dstBinding = glsl::SRV_SLOT,
      .dstArrayElement = index,
      .descriptorCount = (u32)image_info.size(),
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
      .pImageInfo = image_info.data(),
  };
  device->vk.vkUpdateDescriptorSets(device->handle, 1, &write_info, 0, nullptr);
}

void write_cis_descriptor_heap(Device device, TempSpan<const ImageView> srvs,
                               TempSpan<const Sampler> samplers, u32 index) {
  SmallVector<VkDescriptorImageInfo> image_info(srvs.size());
  for (usize i : range(srvs.size())) {
    image_info[i] = {
        .sampler = samplers[i].handle,
        .imageView = srvs[i].handle,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
  }
  VkWriteDescriptorSet write_info = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = device->descriptor_heap,
      .dstBinding = glsl::CIS_SLOT,
      .dstArrayElement = index,
      .descriptorCount = (u32)image_info.size(),
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = image_info.data(),
  };
  device->vk.vkUpdateDescriptorSets(device->handle, 1, &write_info, 0, nullptr);
}

void write_uav_descriptor_heap(Device device, TempSpan<const ImageView> uavs,
                               u32 index) {
  SmallVector<VkDescriptorImageInfo> image_info(uavs.size());
  for (usize i : range(uavs.size())) {
    image_info[i] = {
        .imageView = uavs[i].handle,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
  }
  VkWriteDescriptorSet write_info = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = device->descriptor_heap,
      .dstBinding = glsl::UAV_SLOT,
      .dstArrayElement = index,
      .descriptorCount = (u32)image_info.size(),
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .pImageInfo = image_info.data(),
  };
  device->vk.vkUpdateDescriptorSets(device->handle, 1, &write_info, 0, nullptr);
}

namespace {

constexpr auto PRIMITIVE_TOPOLOGY_MAP = [] {
  std::array<VkPrimitiveTopology, PRIMITIVE_TOPOLOGY_COUNT> map = {};
  map(PrimitiveTopology::PointList, VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
  map(PrimitiveTopology::LineList, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
  map(PrimitiveTopology::TriangleList, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  return map;
}();

constexpr auto FILL_MODE_MAP = [] {
  std::array<VkPolygonMode, FILL_MODE_COUNT> map = {};
  map(FillMode::Fill, VK_POLYGON_MODE_FILL);
  map(FillMode::Wireframe, VK_POLYGON_MODE_LINE);
  return map;
}();

constexpr auto CULL_MODE_MAP = [] {
  std::array<VkCullModeFlagBits, CULL_MODE_COUNT> map = {};
  map(CullMode::None, VK_CULL_MODE_NONE);
  map(CullMode::Front, VK_CULL_MODE_FRONT_BIT);
  map(CullMode::Back, VK_CULL_MODE_BACK_BIT);
  return map;
}();

constexpr auto COMPARE_OP_MAP = [] {
  std::array<VkCompareOp, COMPARE_OP_COUNT> map = {};
  map(CompareOp::Never, VK_COMPARE_OP_NEVER);
  map(CompareOp::Less, VK_COMPARE_OP_LESS);
  map(CompareOp::Equal, VK_COMPARE_OP_EQUAL);
  map(CompareOp::LessOrEqual, VK_COMPARE_OP_LESS_OR_EQUAL);
  map(CompareOp::Greater, VK_COMPARE_OP_GREATER);
  map(CompareOp::NotEqual, VK_COMPARE_OP_NOT_EQUAL);
  map(CompareOp::GreaterOrEqual, VK_COMPARE_OP_GREATER_OR_EQUAL);
  map(CompareOp::Always, VK_COMPARE_OP_ALWAYS);
  return map;
}();

constexpr auto BLEND_FACTOR_MAP = [] {
  std::array<VkBlendFactor, BLEND_FACTOR_COUNT> map = {};
  map(BlendFactor::Zero, VK_BLEND_FACTOR_ZERO);
  map(BlendFactor::One, VK_BLEND_FACTOR_ONE);
  map(BlendFactor::SrcColor, VK_BLEND_FACTOR_SRC_COLOR);
  map(BlendFactor::OneMinusSrcColor, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR);
  map(BlendFactor::DstColor, VK_BLEND_FACTOR_DST_COLOR);
  map(BlendFactor::OneMinusDstColor, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR);
  map(BlendFactor::SrcAlpha, VK_BLEND_FACTOR_SRC_ALPHA);
  map(BlendFactor::OneMinusSrcAlpha, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
  map(BlendFactor::DstAlpha, VK_BLEND_FACTOR_DST_ALPHA);
  map(BlendFactor::OneMinusDstAlpha, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA);
  map(BlendFactor::ConstantColor, VK_BLEND_FACTOR_CONSTANT_COLOR);
  map(BlendFactor::OneMinusConstantColor,
      VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR);
  map(BlendFactor::ConstantAlpha, VK_BLEND_FACTOR_CONSTANT_ALPHA);
  map(BlendFactor::OneMinusConstantAlpha,
      VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA);
  map(BlendFactor::SrcAlphaSaturate, VK_BLEND_FACTOR_SRC_ALPHA_SATURATE);
  map(BlendFactor::Src1Color, VK_BLEND_FACTOR_SRC1_COLOR);
  map(BlendFactor::OneMinusSrc1Color, VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR);
  map(BlendFactor::Src1Alpha, VK_BLEND_FACTOR_SRC1_ALPHA);
  map(BlendFactor::OneMinusSrc1Alpha, VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA);
  return map;
}();

constexpr auto BLEND_OP_MAP = [] {
  std::array<VkBlendOp, BLEND_OP_COUNT> map = {};
  map(BlendOp::Add, VK_BLEND_OP_ADD);
  map(BlendOp::Subtract, VK_BLEND_OP_SUBTRACT);
  map(BlendOp::ReverseSubtract, VK_BLEND_OP_REVERSE_SUBTRACT);
  map(BlendOp::Min, VK_BLEND_OP_MIN);
  map(BlendOp::Max, VK_BLEND_OP_MAX);
  return map;
}();

constexpr auto LOGIC_OP_MAP = [] {
  std::array<VkLogicOp, LOGIC_OP_COUNT> map = {};
  map(LogicOp::Clear, VK_LOGIC_OP_CLEAR);
  map(LogicOp::And, VK_LOGIC_OP_AND);
  map(LogicOp::AndReverse, VK_LOGIC_OP_AND_REVERSE);
  map(LogicOp::Copy, VK_LOGIC_OP_COPY);
  map(LogicOp::AndInverted, VK_LOGIC_OP_AND_INVERTED);
  map(LogicOp::NoOp, VK_LOGIC_OP_NO_OP);
  map(LogicOp::Xor, VK_LOGIC_OP_XOR);
  map(LogicOp::Or, VK_LOGIC_OP_OR);
  map(LogicOp::Nor, VK_LOGIC_OP_NOR);
  map(LogicOp::Equivalent, VK_LOGIC_OP_EQUIVALENT);
  map(LogicOp::Invert, VK_LOGIC_OP_INVERT);
  map(LogicOp::OrReverse, VK_LOGIC_OP_OR_REVERSE);
  map(LogicOp::CopyInverted, VK_LOGIC_OP_COPY_INVERTED);
  map(LogicOp::OrInverted, VK_LOGIC_OP_OR_INVERTED);
  map(LogicOp::Nand, VK_LOGIC_OP_NAND);
  map(LogicOp::Set, VK_LOGIC_OP_SET);
  return map;
}();

constexpr auto COLOR_COMPONENT_MAP = [] {
  using enum ImageUsage;
  std::array<VkColorComponentFlagBits, COLOR_COMPONENT_COUNT> map = {};
  map_bit(ColorComponent::R, VK_COLOR_COMPONENT_R_BIT);
  map_bit(ColorComponent::G, VK_COLOR_COMPONENT_G_BIT);
  map_bit(ColorComponent::B, VK_COLOR_COMPONENT_B_BIT);
  map_bit(ColorComponent::A, VK_COLOR_COMPONENT_A_BIT);
  return map;
}();

auto to_vk_color_component_mask(ColorComponentMask mask)
    -> VkColorComponentFlags {
  VkColorComponentFlags vk_mask = 0;
  for (usize bit : range(COLOR_COMPONENT_COUNT)) {
    auto comp = (ColorComponent)(1 << bit);
    if (mask.is_set(comp)) {
      vk_mask |= COLOR_COMPONENT_MAP[bit];
    }
  }
  return vk_mask;
}

} // namespace

auto create_graphics_pipeline(Device device,
                              const GraphicsPipelineCreateInfo &create_info)
    -> Result<Pipeline> {
  VkResult result = VK_SUCCESS;

  const ShaderInfo *shaders[] = {
      &create_info.ts,
      &create_info.ms,
      &create_info.vs,
      &create_info.fs,
  };
  constexpr usize MAX_NUM_STAGES = std::size(shaders);
  constexpr VkShaderStageFlagBits stage_bits[] = {
      VK_SHADER_STAGE_TASK_BIT_EXT,
      VK_SHADER_STAGE_MESH_BIT_EXT,
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  VkShaderModule modules[MAX_NUM_STAGES] = {};
  VkPipelineShaderStageCreateInfo stage_info[MAX_NUM_STAGES] = {};
  VkSpecializationInfo specialization_info[MAX_NUM_STAGES] = {};

  SmallVector<VkSpecializationMapEntry> specialization_map;
  {
    u32 specialization_map_size = 0;
    for (const ShaderInfo *shader : shaders) {
      specialization_map_size += shader->specialization.constants.size();
    }
    specialization_map.resize(specialization_map_size);
  }

  u32 num_stages = 0;
  u32 specialization_map_offset = 0;
  for (usize i : range(MAX_NUM_STAGES)) {
    const ShaderInfo &shader = *shaders[i];
    if (shader.code.empty()) {
      continue;
    }
    u32 num_specialization_constants = shader.specialization.constants.size();
    for (usize j : range(num_specialization_constants)) {
      const SpecializationConstant &c = shader.specialization.constants[j];
      specialization_map[specialization_map_offset + j] = {
          .constantID = c.id,
          .offset = c.offset,
          .size = c.size,
      };
    }
    VkShaderModuleCreateInfo module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader.code.size(),
        .pCode = (const u32 *)shader.code.data(),
    };
    result = device->vk.vkCreateShaderModule(device->handle, &module_info,
                                             nullptr, &modules[num_stages]);
    if (result) {
      for (VkShaderModule module : modules) {
        device->vk.vkDestroyShaderModule(device->handle, module, nullptr);
      }
      return fail(result);
    }
    specialization_info[num_stages] = {
        .mapEntryCount = num_specialization_constants,
        .pMapEntries = specialization_map.data() + specialization_map_offset,
        .dataSize = shader.specialization.data.size(),
        .pData = shader.specialization.data.data(),
    };
    specialization_map_offset += num_specialization_constants;
    stage_info[num_stages] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage_bits[i],
        .module = modules[num_stages],
        .pName = shader.entry_point,
        .pSpecializationInfo = &specialization_info[num_stages],
    };
    num_stages++;
  }

  VkPipelineVertexInputStateCreateInfo vertex_input_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = PRIMITIVE_TOPOLOGY_MAP[(usize)create_info.input_assembly_state
                                             .topology],
  };

  VkPipelineViewportStateCreateInfo viewport_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = create_info.rasterization_state.depth_clamp_enable,
      .polygonMode =
          FILL_MODE_MAP[(usize)create_info.rasterization_state.fill_mode],
      .cullMode = (VkCullModeFlags)
          CULL_MODE_MAP[(usize)create_info.rasterization_state.cull_mode],
      .frontFace = create_info.rasterization_state.front_face == FrontFace::CCW
                       ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                       : VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable = create_info.rasterization_state.depth_bias_enable,
      .depthBiasConstantFactor =
          (float)create_info.rasterization_state.depth_bias_constant_factor,
      .depthBiasClamp = create_info.rasterization_state.depth_bias_clamp,
      .depthBiasSlopeFactor =
          create_info.rasterization_state.depth_bias_slope_factor,
      .lineWidth = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples =
          (VkSampleCountFlagBits)create_info.multisampling_state.sample_count,
      .pSampleMask = &create_info.multisampling_state.sample_mask,
      .alphaToCoverageEnable =
          create_info.multisampling_state.alpha_to_coverage_enable,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = create_info.depth_stencil_state.depth_test_enable,
      .depthWriteEnable = create_info.depth_stencil_state.depth_write_enable,
      .depthCompareOp = COMPARE_OP_MAP[(usize)create_info.depth_stencil_state
                                           .depth_compare_op],
      .depthBoundsTestEnable =
          create_info.depth_stencil_state.depth_bounds_test_enable,
      .minDepthBounds = create_info.depth_stencil_state.min_depth_bounds,
      .maxDepthBounds = create_info.depth_stencil_state.max_depth_bounds,
  };

  VkFormat color_formats[MAX_NUM_RENDER_TARGETS] = {};
  for (usize i : range(MAX_NUM_RENDER_TARGETS)) {
    color_formats[i] = toVkFormat(create_info.rtv_formats[i]);
  }

  VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = create_info.num_render_targets,
      .pColorAttachmentFormats = color_formats,
      .depthAttachmentFormat = toVkFormat(create_info.dsv_format),
  };

  VkPipelineColorBlendAttachmentState
      attachment_blend_info[MAX_NUM_RENDER_TARGETS] = {};
  for (usize i : range(create_info.num_render_targets)) {
    const RenderTargetBlendInfo &target = create_info.blend_state.targets[i];
    attachment_blend_info[i] = {
        .blendEnable = target.blend_enable,
        .srcColorBlendFactor =
            BLEND_FACTOR_MAP[(usize)target.src_color_blend_factor],
        .dstColorBlendFactor =
            BLEND_FACTOR_MAP[(usize)target.dst_color_blend_factor],
        .colorBlendOp = BLEND_OP_MAP[(usize)target.color_blend_op],
        .srcAlphaBlendFactor =
            BLEND_FACTOR_MAP[(usize)target.src_alpha_blend_factor],
        .dstAlphaBlendFactor =
            BLEND_FACTOR_MAP[(usize)target.dst_alpha_blend_factor],
        .alphaBlendOp = BLEND_OP_MAP[(usize)target.alpha_blend_op],
        .colorWriteMask = to_vk_color_component_mask(target.color_write_mask),
    };
  }

  VkPipelineColorBlendStateCreateInfo blend_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = create_info.blend_state.logic_op_enable,
      .logicOp = LOGIC_OP_MAP[(usize)create_info.blend_state.logic_op],
      .attachmentCount = create_info.num_render_targets,
      .pAttachments = attachment_blend_info,
  };
  std::memcpy(blend_info.blendConstants,
              &create_info.blend_state.blend_constants,
              sizeof(blend_info.blendConstants));

  VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
      VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
  };

  VkPipelineDynamicStateCreateInfo dynamic_state_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = std::size(dynamic_states),
      .pDynamicStates = dynamic_states,
  };

  VkGraphicsPipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = num_stages,
      .pStages = stage_info,
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &input_assembly_info,
      .pViewportState = &viewport_info,
      .pRasterizationState = &rasterization_info,
      .pMultisampleState = &multisample_info,
      .pDepthStencilState = &depth_stencil_info,
      .pColorBlendState = &blend_info,
      .pDynamicState = &dynamic_state_info,
      .layout = device->pipeline_layout,
  };

  Pipeline pipeline;
  result = device->vk.vkCreateGraphicsPipelines(
      device->handle, nullptr, 1, &pipeline_info, nullptr, &pipeline.handle);
  for (VkShaderModule module : modules) {
    device->vk.vkDestroyShaderModule(device->handle, module, nullptr);
  }
  if (result) {
    return fail(result);
  }

  return pipeline;
}

auto create_compute_pipeline(Device device,
                             const ComputePipelineCreateInfo &create_info)
    -> Result<Pipeline> {
  VkResult result = VK_SUCCESS;

  const ShaderInfo &cs = create_info.cs;

  VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = cs.code.size(),
      .pCode = (const u32 *)cs.code.data(),
  };
  VkShaderModule module;
  result = device->vk.vkCreateShaderModule(device->handle, &module_info,
                                           nullptr, &module);
  if (result) {
    return fail(result);
  }

  SmallVector<VkSpecializationMapEntry> specialization_map(
      cs.specialization.constants.size());
  for (usize i : range(cs.specialization.constants.size())) {
    const SpecializationConstant &c = cs.specialization.constants[i];
    specialization_map[i] = {
        .constantID = c.id,
        .offset = c.offset,
        .size = c.size,
    };
  }
  VkSpecializationInfo specialization_info = {
      .mapEntryCount = (u32)specialization_map.size(),
      .pMapEntries = specialization_map.data(),
      .dataSize = cs.specialization.data.size(),
      .pData = cs.specialization.data.data(),
  };

  VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
          {
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_COMPUTE_BIT,
              .module = module,
              .pName = cs.entry_point,
              .pSpecializationInfo = &specialization_info,
          },
      .layout = device->pipeline_layout,
  };

  Pipeline pipeline;
  result = device->vk.vkCreateComputePipelines(
      device->handle, nullptr, 1, &pipeline_info, nullptr, &pipeline.handle);
  device->vk.vkDestroyShaderModule(device->handle, module, nullptr);
  if (result) {
    return fail(result);
  }

  return pipeline;
}

void destroy_pipeline(Device device, Pipeline pipeline) {
  device->vk.vkDestroyPipeline(device->handle, pipeline.handle, nullptr);
}

auto set_debug_name(Device device, Pipeline pipeline, const char *name)
    -> Result<void> {
  return set_debug_name(device, VK_OBJECT_TYPE_PIPELINE, pipeline.handle, name);
}

namespace vk {

struct CommandPoolData {
  VkCommandPool handle = nullptr;
  SmallVector<VkCommandBuffer> cmd_buffers;
  usize cmd_index = 0;
  QueueFamily queue_family = {};
};

} // namespace vk

auto create_command_pool(Device device,
                         const CommandPoolCreateInfo &create_info)
    -> Result<CommandPool> {
  const AdapterData &adapter = get_adapter(device);
  CommandPool pool = new CommandPoolData{
      .queue_family = create_info.queue_family,
  };
  VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex =
          adapter.queue_family_indices[(usize)create_info.queue_family],
  };
  VkResult result = device->vk.vkCreateCommandPool(device->handle, &pool_info,
                                                   nullptr, &pool->handle);
  if (result) {
    return fail(result);
  }
  return pool;
}

void destroy_command_pool(Device device, CommandPool pool) {
  if (pool) {
    device->vk.vkDestroyCommandPool(device->handle, pool->handle, nullptr);
    delete pool;
  }
}

auto set_debug_name(Device device, CommandPool pool, const char *name)
    -> Result<void> {
  return set_debug_name(device, VK_OBJECT_TYPE_COMMAND_POOL, pool->handle,
                        name);
}

auto reset_command_pool(Device device, CommandPool pool) -> Result<void> {
  VkResult result =
      device->vk.vkResetCommandPool(device->handle, pool->handle, 0);
  if (result) {
    return fail(result);
  }
  pool->cmd_index = 0;
  return {};
}

auto begin_command_buffer(Device device, CommandPool pool)
    -> Result<CommandBuffer> {
  VkResult result = VK_SUCCESS;

  [[unlikely]] if (pool->cmd_index == pool->cmd_buffers.size()) {
    u32 old_size = pool->cmd_buffers.size();
    u32 new_size = std::max(old_size * 3 / 2 + 1, 1u);
    pool->cmd_buffers.resize(new_size);
    VkCommandBufferAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool->handle,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = new_size - old_size,
    };
    result = device->vk.vkAllocateCommandBuffers(device->handle, &allocate_info,
                                                 &pool->cmd_buffers[old_size]);
    if (result) {
      return fail(result);
    }
  }

  CommandBuffer cmd = {
      .handle = pool->cmd_buffers[pool->cmd_index++],
      .device = device,
  };

  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  result = device->vk.vkBeginCommandBuffer(cmd.handle, &begin_info);
  if (result) {
    return fail(result);
  }

  if (pool->queue_family == QueueFamily::Graphics or
      pool->queue_family == QueueFamily::Compute) {
    device->vk.vkCmdBindDescriptorSets(
        cmd.handle, VK_PIPELINE_BIND_POINT_COMPUTE, device->pipeline_layout, 0,
        1, &device->descriptor_heap, 0, nullptr);
  }
  if (pool->queue_family == QueueFamily::Graphics) {
    device->vk.vkCmdBindDescriptorSets(
        cmd.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, device->pipeline_layout, 0,
        1, &device->descriptor_heap, 0, nullptr);
  }

  return cmd;
}

auto end_command_buffer(CommandBuffer cmd) -> Result<void> {
  VkResult result = cmd.device->vk.vkEndCommandBuffer(cmd.handle);
  if (result) {
    return fail(result);
  }
  return {};
}

namespace {

template <typename E>
  requires std::is_scoped_enum_v<E>
constexpr usize ENUM_SIZE = []() -> usize {
  if (CFlagsEnum<E>) {
    return std::countr_zero((usize)E::Last) + 1;
  }
  return (usize)E::Last + 1;
}();

template <typename From> constexpr auto MAP = nullptr;

template <typename From>
  requires std::is_scoped_enum_v<From>
auto to_vk(From e) {
  return MAP<From>[(usize)e];
};

template <CFlagsEnum From> auto to_vk(Flags<From> mask) {
  typename decltype(MAP<From>)::value_type vk_mask = 0;
  for (usize bit : range(ENUM_SIZE<From>)) {
    if (mask.is_set((From)(1ull << bit))) {
      vk_mask |= MAP<From>[bit];
    }
  }
  return vk_mask;
};

template <>
constexpr auto MAP<ImageAspect> = [] {
  std::array<VkImageAspectFlags, ENUM_SIZE<ImageAspect>> map = {};
  map_bit(ImageAspect::Color, VK_IMAGE_ASPECT_COLOR_BIT);
  map_bit(ImageAspect::Depth, VK_IMAGE_ASPECT_DEPTH_BIT);
  return map;
}();

template <>
constexpr auto MAP<PipelineStage> = [] {
  std::array<VkPipelineStageFlagBits2, ENUM_SIZE<PipelineStage>> map = {};
  map_bit(PipelineStage::ExecuteIndirect,
          VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
  map_bit(PipelineStage::TaskShader, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT);
  map_bit(PipelineStage::MeshShader, VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
  map_bit(PipelineStage::IndexInput, VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT);
  map_bit(PipelineStage::VertexShader, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
  map_bit(PipelineStage::EarlyFragmentTests,
          VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT);
  map_bit(PipelineStage::FragmentShader,
          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
  map_bit(PipelineStage::LateFragmentTests,
          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
  map_bit(PipelineStage::RenderTargetOutput,
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
  map_bit(PipelineStage::ComputeShader, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
  map_bit(PipelineStage::Transfer, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
  map_bit(PipelineStage::All, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
  return map;
}();

template <>
constexpr auto MAP<Access> = [] {
  std::array<VkAccessFlagBits2, ENUM_SIZE<Access>> map = {};
  map_bit(Access::IndirectCommandRead, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
  map_bit(Access::IndexRead, VK_ACCESS_2_INDEX_READ_BIT);
  map_bit(Access::UniformRead, VK_ACCESS_2_UNIFORM_READ_BIT);
  map_bit(Access::ShaderBufferRead, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
  map_bit(Access::ShaderImageRead, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  map_bit(Access::UnorderedAccess, VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
  map_bit(Access::RenderTarget, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  map_bit(Access::DepthStencilRead,
          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
  map_bit(Access::DepthStencilWrite,
          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
  map_bit(Access::TransferRead, VK_ACCESS_2_TRANSFER_READ_BIT);
  map_bit(Access::TransferWrite, VK_ACCESS_2_TRANSFER_WRITE_BIT);
  map_bit(Access::MemoryRead, VK_ACCESS_2_MEMORY_READ_BIT);
  map_bit(Access::MemoryWrite, VK_ACCESS_2_MEMORY_WRITE_BIT);
  return map;
}();

template <>
constexpr auto MAP<ImageLayout> = [] {
  std::array<VkImageLayout, ENUM_SIZE<ImageLayout>> map = {};
  map(ImageLayout::Undefined, VK_IMAGE_LAYOUT_UNDEFINED);
  map(ImageLayout::ShaderResource, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  map(ImageLayout::UnorderedAccess, VK_IMAGE_LAYOUT_GENERAL);
  map(ImageLayout::RenderTarget, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  map(ImageLayout::DepthStencilRead,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
  map(ImageLayout::DepthStencilWrite,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  map(ImageLayout::TransferSrc, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  map(ImageLayout::TransferDst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  map(ImageLayout::Present, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  return map;
}();

template <>
constexpr auto MAP<PipelineBindPoint> = [] {
  std::array<VkPipelineBindPoint, PIPELINE_BIND_POINT_COUNT> map = {};
  map(PipelineBindPoint::Graphics, VK_PIPELINE_BIND_POINT_GRAPHICS);
  map(PipelineBindPoint::Compute, VK_PIPELINE_BIND_POINT_COMPUTE);
  return map;
}();

} // namespace

void cmd_pipeline_barrier(CommandBuffer cmd,
                          TempSpan<const MemoryBarrier> memory_barriers,
                          TempSpan<const ImageBarrier> image_barriers) {
  Adapter adapter = cmd.device->adapter;
  SmallVector<VkMemoryBarrier2, 16> vk_memory_barriers(memory_barriers.size());
  for (usize i : range(memory_barriers.size())) {
    const MemoryBarrier &barrier = memory_barriers[i];
    vk_memory_barriers[i] = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = to_vk(barrier.src_stage_mask),
        .srcAccessMask = to_vk(barrier.src_access_mask),
        .dstStageMask = to_vk(barrier.dst_stage_mask),
        .dstAccessMask = to_vk(barrier.dst_access_mask),
    };
  }
  SmallVector<VkImageMemoryBarrier2, 16> vk_image_barriers(
      image_barriers.size());
  for (usize i : range(image_barriers.size())) {
    const ImageBarrier &barrier = image_barriers[i];
    vk_image_barriers[i] = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = to_vk(barrier.src_stage_mask),
        .srcAccessMask = to_vk(barrier.src_access_mask),
        .dstStageMask = to_vk(barrier.dst_stage_mask),
        .dstAccessMask = to_vk(barrier.dst_access_mask),
        .oldLayout = to_vk(barrier.src_layout),
        .newLayout = to_vk(barrier.dst_layout),
        .image = barrier.image.handle,
        .subresourceRange =
            {
                .aspectMask = to_vk(barrier.range.aspect_mask),
                .baseMipLevel = barrier.range.base_mip,
                .levelCount = barrier.range.num_mips,
                .baseArrayLayer = barrier.range.base_layer,
                .layerCount = barrier.range.num_layers,
            },
    };
  }
  VkDependencyInfo dependency_info = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = (u32)vk_memory_barriers.size(),
      .pMemoryBarriers = vk_memory_barriers.data(),
      .imageMemoryBarrierCount = (u32)vk_image_barriers.size(),
      .pImageMemoryBarriers = vk_image_barriers.data(),
  };
  cmd.device->vk.vkCmdPipelineBarrier2(cmd.handle, &dependency_info);
}

void cmd_copy_buffer(CommandBuffer cmd, const BufferCopyInfo &info) {
  VkBufferCopy region = {
      .srcOffset = info.src_offset,
      .dstOffset = info.dst_offset,
      .size = info.size,
  };
  cmd.device->vk.vkCmdCopyBuffer(cmd.handle, info.src.handle, info.dst.handle,
                                 1, &region);
}

void cmd_copy_buffer_to_image(CommandBuffer cmd,
                              const BufferImageCopyInfo &info) {
  VkBufferImageCopy region = {
      .bufferOffset = info.buffer_offset,
      .imageSubresource =
          {
              .aspectMask = to_vk(info.aspect_mask),
              .mipLevel = info.mip,
              .baseArrayLayer = info.base_layer,
              .layerCount = info.num_layers,
          },
      .imageOffset = {(i32)info.image_offset.x, (i32)info.image_offset.y,
                      (i32)info.image_offset.z},
      .imageExtent = {info.image_size.x, info.image_size.y, info.image_size.z},
  };
  cmd.device->vk.vkCmdCopyBufferToImage(
      cmd.handle, info.buffer.handle, info.image.handle,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void cmd_copy_image_to_buffer(CommandBuffer cmd,
                              const BufferImageCopyInfo &info) {
  VkBufferImageCopy region = {
      .bufferOffset = info.buffer_offset,
      .imageSubresource =
          {
              .aspectMask = to_vk(info.aspect_mask),
              .mipLevel = info.mip,
              .baseArrayLayer = info.base_layer,
              .layerCount = info.num_layers,
          },
      .imageOffset = {(i32)info.image_offset.x, (i32)info.image_offset.y,
                      (i32)info.image_offset.z},
      .imageExtent = {info.image_size.x, info.image_size.y, info.image_size.z},
  };
  cmd.device->vk.vkCmdCopyImageToBuffer(cmd.handle, info.image.handle,
                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                        info.buffer.handle, 1, &region);
}

void cmd_fill_buffer(CommandBuffer cmd, const BufferFillInfo &info) {
  cmd.device->vk.vkCmdFillBuffer(cmd.handle, info.buffer.handle, info.offset,
                                 info.size, info.value);
}

void cmd_clear_image(CommandBuffer cmd, const ImageClearInfo &info) {
  VkClearColorValue color = {
      .float32 = {info.color.r, info.color.g, info.color.b, info.color.a},
  };
  VkImageSubresourceRange subresource = {
      .aspectMask = to_vk(info.aspect_mask),
      .baseMipLevel = info.base_mip,
      .levelCount = info.num_mips,
      .baseArrayLayer = info.base_layer,
      .layerCount = info.num_layers,
  };
  cmd.device->vk.vkCmdClearColorImage(cmd.handle, info.image.handle,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      &color, 1, &subresource);
}

void cmd_push_constants(CommandBuffer cmd, usize offset,
                        Span<const std::byte> data) {
  cmd.device->vk.vkCmdPushConstants(cmd.handle, cmd.device->pipeline_layout,
                                    VK_SHADER_STAGE_ALL, offset, data.size(),
                                    data.data());
}

namespace {

template <>
constexpr auto MAP<IndexType> = [] {
  std::array<VkIndexType, ENUM_SIZE<IndexType>> map = {};
  map(IndexType::UInt8, VK_INDEX_TYPE_UINT8_EXT);
  map(IndexType::UInt16, VK_INDEX_TYPE_UINT16);
  map(IndexType::UInt32, VK_INDEX_TYPE_UINT32);
  return map;
}();

} // namespace

void cmd_bind_index_buffer(CommandBuffer cmd, Buffer buffer, usize offset,
                           IndexType index_type) {
  cmd.device->vk.vkCmdBindIndexBuffer(cmd.handle, buffer.handle, offset,
                                      to_vk(index_type));
}

void cmd_bind_pipeline(CommandBuffer cmd, PipelineBindPoint bind_point,
                       Pipeline pipeline) {
  cmd.device->vk.vkCmdBindPipeline(cmd.handle, to_vk(bind_point),
                                   pipeline.handle);
}

void cmd_draw(CommandBuffer cmd, const DrawInfo &draw_info) {
  cmd.device->vk.vkCmdDraw(cmd.handle, draw_info.num_vertices,
                           draw_info.num_instances, draw_info.base_vertex,
                           draw_info.base_instance);
}

void cmd_draw_indexed(CommandBuffer cmd, const DrawIndexedInfo &draw_info) {
  cmd.device->vk.vkCmdDrawIndexed(
      cmd.handle, draw_info.num_indices, draw_info.num_instances,
      draw_info.base_index, draw_info.vertex_offset, draw_info.base_instance);
}

void cmd_draw_indirect_count(CommandBuffer cmd,
                             const DrawIndirectCountInfo &draw_info) {
  cmd.device->vk.vkCmdDrawIndirectCount(
      cmd.handle, draw_info.buffer.handle, draw_info.buffer_offset,
      draw_info.count_buffer.handle, draw_info.count_buffer_offset,
      draw_info.max_count, draw_info.buffer_stride);
}

void cmd_draw_indexed_indirect_count(CommandBuffer cmd,
                                     const DrawIndirectCountInfo &draw_info) {
  cmd.device->vk.vkCmdDrawIndexedIndirectCount(
      cmd.handle, draw_info.buffer.handle, draw_info.buffer_offset,
      draw_info.count_buffer.handle, draw_info.count_buffer_offset,
      draw_info.max_count, draw_info.buffer_stride);
}

void cmd_dispatch(CommandBuffer cmd, u32 num_groups_x, u32 num_groups_y,
                  u32 num_groups_z) {
  cmd.device->vk.vkCmdDispatch(cmd.handle, num_groups_x, num_groups_y,
                               num_groups_z);
}

void cmd_dispatch_indirect(CommandBuffer cmd, Buffer buffer, usize offset) {
  cmd.device->vk.vkCmdDispatchIndirect(cmd.handle, buffer.handle, offset);
}

void cmd_set_viewports(CommandBuffer cmd, TempSpan<const Viewport> viewports) {
  VkViewport vk_viewports[rhi::MAX_NUM_RENDER_TARGETS];
  for (usize i : range(viewports.size())) {
    const Viewport &vp = viewports[i];
    vk_viewports[i] = {
        .x = vp.offset.x,
        .y = vp.offset.y + vp.size.y,
        .width = vp.size.x,
        .height = -vp.size.y,
        .minDepth = vp.min_depth,
        .maxDepth = vp.max_depth,
    };
  }
  cmd.device->vk.vkCmdSetViewportWithCount(cmd.handle, viewports.size(),
                                           vk_viewports);
}

void cmd_set_scissor_rects(CommandBuffer cmd, TempSpan<const Rect2D> rects) {
  VkRect2D vk_rects[rhi::MAX_NUM_RENDER_TARGETS];
  for (usize i : range(rects.size())) {
    const Rect2D &rect = rects[i];
    vk_rects[i] = {
        .offset = {(i32)rect.offset.x, (i32)rect.offset.y},
        .extent = {rect.size.x, rect.size.y},
    };
  }
  cmd.device->vk.vkCmdSetScissorWithCount(cmd.handle, rects.size(), vk_rects);
}

void cmd_begin_debug_label(CommandBuffer cmd, const char *label) {
  if (vkCmdBeginDebugUtilsLabelEXT) {
    ren_assert(label);
    VkDebugUtilsLabelEXT label_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = label,
    };
    vkCmdBeginDebugUtilsLabelEXT(cmd.handle, &label_info);
  }
}

void cmd_end_debug_label(CommandBuffer cmd) {
  if (vkCmdEndDebugUtilsLabelEXT) {
    vkCmdEndDebugUtilsLabelEXT(cmd.handle);
  }
}

extern const u32 SDL_WINDOW_FLAGS = SDL_WINDOW_VULKAN;

auto create_surface(SDL_Window *window) -> Result<Surface> {
  Surface surface;
  if (!SDL_Vulkan_CreateSurface(window, g_instance.handle, &surface.handle)) {
    return fail(Error::Unknown);
  }
  return surface;
}

void destroy_surface(Surface surface) {
  vkDestroySurfaceKHR(g_instance.handle, surface.handle, nullptr);
}

auto is_queue_family_present_supported(Adapter handle, QueueFamily family,
                                       Surface surface) -> bool {
  ren_assert(g_instance.handle);
  ren_assert(handle.index < g_instance.adapters.size());
  ren_assert(surface.handle);
  VkResult result = VK_SUCCESS;
  if (!is_queue_family_supported(handle, family)) {
    return false;
  }
  const AdapterData &adapter = g_instance.adapters[handle.index];
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
  map(Immediate, VK_PRESENT_MODE_IMMEDIATE_KHR);
  map(Mailbox, VK_PRESENT_MODE_MAILBOX_KHR);
  map(Fifo, VK_PRESENT_MODE_FIFO_KHR);
  map(FifoRelaxed, VK_PRESENT_MODE_FIFO_RELAXED_KHR);
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
      g_instance.adapters[adapter.index].physical_device, surface.handle,
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
      g_instance.adapters[adapter.index].physical_device;
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
      g_instance.adapters[adapter.index].physical_device;
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
  Surface surface = {};
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
  const AdapterData &adapter = g_instance.adapters[device->adapter.index];
  VkSurfacePresentModeEXT present_mode_info = {
      .sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_EXT,
      .presentMode = present_mode,
  };
  VkPhysicalDeviceSurfaceInfo2KHR surface_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
      .pNext = &present_mode_info,
      .surface = swap_chain->surface.handle,
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
  StaticVector<u32, 2> queue_family_indices = {
      adapter.queue_family_indices[(usize)QueueFamily::Graphics]};
  if (is_queue_family_present_supported(device->adapter, QueueFamily::Compute,
                                        swap_chain->surface)) {
    queue_family_indices.push_back(
        adapter.queue_family_indices[(usize)QueueFamily::Compute]);
  }
  VkSwapchainKHR old_swap_chain = swap_chain->handle;
  VkSwapchainCreateInfoKHR vk_create_info = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = swap_chain->surface.handle,
      .minImageCount = num_images,
      .imageFormat = swap_chain->format,
      .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
      .imageExtent = {size.x, size.y},
      .imageArrayLayers = 1,
      .imageUsage = swap_chain->usage,
      .imageSharingMode = queue_family_indices.size() > 1
                              ? VK_SHARING_MODE_CONCURRENT
                              : VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = (u32)queue_family_indices.size(),
      .pQueueFamilyIndices = queue_family_indices.data(),
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
      .surface = create_info.surface,
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

auto resize_swap_chain(SwapChain swap_chain, glm::uvec2 size, u32 num_images,
                       ImageUsageFlags usage) -> Result<void> {
  VkImageUsageFlags vk_usage = to_vk_image_usage_flags(usage);
  if (!vk_usage) {
    vk_usage = swap_chain->usage;
  }
  swap_chain->usage = vk_usage;
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

auto present(Queue queue, SwapChain swap_chain, Semaphore semaphore)
    -> Result<void> {
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
  VkResult result = device->vk.vkQueuePresentKHR(queue.handle, &present_info);
  swap_chain->image = vk::SWAP_CHAIN_IMAGE_NOT_ACQUIRED;
  if (result and result != VK_SUBOPTIMAL_KHR) {
    return fail(result);
  }
  return {};
}

namespace {

auto amd_anti_lag(Device device, u64 frame, VkAntiLagStageAMD stage,
                  bool enable, u32 max_fps) -> Result<void> {
  VkAntiLagPresentationInfoAMD present_info = {
      .sType = VK_STRUCTURE_TYPE_ANTI_LAG_PRESENTATION_INFO_AMD,
      .stage = stage,
      .frameIndex = frame,
  };
  VkAntiLagDataAMD anti_lag_data = {
      .sType = VK_STRUCTURE_TYPE_ANTI_LAG_DATA_AMD,
      .mode = enable ? VK_ANTI_LAG_MODE_ON_AMD : VK_ANTI_LAG_MODE_OFF_AMD,
      .maxFPS = max_fps,
      .pPresentationInfo = &present_info,
  };
  device->vk.vkAntiLagUpdateAMD(device->handle, &anti_lag_data);
  return {};
}

} // namespace

auto amd_anti_lag_input(Device device, u64 frame, bool enable, u32 max_fps)
    -> Result<void> {
  return amd_anti_lag(device, frame, VK_ANTI_LAG_STAGE_INPUT_AMD, enable,
                      max_fps);
}

auto amd_anti_lag_present(Device device, u64 frame, bool enable, u32 max_fps)
    -> Result<void> {
  return amd_anti_lag(device, frame, VK_ANTI_LAG_STAGE_PRESENT_AMD, enable,
                      max_fps);
}

#undef map
auto map(Device device, Allocation allocation) -> void * {
  VmaAllocationInfo allocation_info;
  vmaGetAllocationInfo(device->allocator, allocation.handle, &allocation_info);
  return allocation_info.pMappedData;
}

} // namespace ren::rhi

#endif // REN_RHI_VULKAN
