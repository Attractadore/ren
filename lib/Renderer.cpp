#include "Renderer.hpp"
#include "Formats.hpp"
#include "Profiler.hpp"
#include "Scene.hpp"
#include "Swapchain.hpp"
#include "core/Array.hpp"
#include "core/Errors.hpp"
#include "core/Views.hpp"

#include <volk.h>

namespace ren {
namespace {

template <typename T>
constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_UNKNOWN;
#define define_object_type(T, type)                                            \
  template <> inline constexpr VkObjectType ObjectType<T> = type
define_object_type(VkBuffer, VK_OBJECT_TYPE_BUFFER);
define_object_type(VkDescriptorPool, VK_OBJECT_TYPE_DESCRIPTOR_POOL);
define_object_type(VkDescriptorSetLayout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);
define_object_type(VkImage, VK_OBJECT_TYPE_IMAGE);
define_object_type(VkPipeline, VK_OBJECT_TYPE_PIPELINE);
define_object_type(VkPipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT);
define_object_type(VkSampler, VK_OBJECT_TYPE_SAMPLER);
define_object_type(VkSemaphore, VK_OBJECT_TYPE_SEMAPHORE);
#undef define_object_type

template <typename T>
void set_debug_name(VkDevice device, T object, const DebugName &name) {
#if REN_DEBUG_NAMES
  static_assert(ObjectType<T>);
  VkDebugUtilsObjectNameInfoEXT name_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = ObjectType<T>,
      .objectHandle = (uint64_t)object,
      .pObjectName = name.c_str(),
  };
  throw_if_failed(vkSetDebugUtilsObjectNameEXT(device, &name_info),
                  "Vulkan: Failed to set object debug name");
#endif
}

} // namespace

namespace {

auto create_instance(Span<const char *const> external_extensions)
    -> VkInstance {
  VkApplicationInfo application_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .apiVersion = VK_API_VERSION_1_3,
  };

  auto layers = make_array<const char *>(
#if REN_VULKAN_VALIDATION
      "VK_LAYER_KHRONOS_validation"
#endif
  );

  SmallVector<const char *> extensions(external_extensions);
#if REN_DEBUG_NAMES
  extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
#if REN_VULKAN_VALIDATION
  extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif

  VkInstanceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &application_info,
      .enabledLayerCount = static_cast<uint32_t>(layers.size()),
      .ppEnabledLayerNames = layers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };

  VkInstance instance;
  throw_if_failed(vkCreateInstance(&create_info, nullptr, &instance),
                  "Vulkan: Failed to create VkInstance");

  return instance;
}

#if REN_VULKAN_VALIDATION
auto create_debug_report_callback(VkInstance instance)
    -> VkDebugReportCallbackEXT {
  VkDebugReportCallbackCreateInfoEXT create_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
      .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
      .pfnCallback = [](VkDebugReportFlagsEXT flags,
                        VkDebugReportObjectTypeEXT objectType, uint64_t object,
                        size_t location, int32_t messageCode,
                        const char *pLayerPrefix, const char *pMessage,
                        void *pUserData) -> VkBool32 {
        fmt::println(stderr, "{}", pMessage);
#if 0
        if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
          ren_trap();
        }
#endif
        return false;
      },
  };
  VkDebugReportCallbackEXT cb = nullptr;
  throw_if_failed(
      vkCreateDebugReportCallbackEXT(instance, &create_info, nullptr, &cb),
      "Vulkan: Failed to create VkDebugReportCallbackEXT");
  return cb;
}
#endif

auto find_adapter(VkInstance instance, u32 adapter) -> VkPhysicalDevice {
  uint32_t num_adapters = 0;
  throw_if_failed(vkEnumeratePhysicalDevices(instance, &num_adapters, nullptr),
                  "Vulkan: Failed to enumerate physical device");
  SmallVector<VkPhysicalDevice> adapters(num_adapters);
  throw_if_failed(
      vkEnumeratePhysicalDevices(instance, &num_adapters, adapters.data()),
      "Vulkan: Failed to enumerate physical device");
  adapters.resize(num_adapters);
  if (adapter == DEFAULT_ADAPTER) {
    return adapters[0];
  }
  if (adapter < adapters.size()) {
    return adapters[adapter];
  }
  return nullptr;
}

auto find_graphics_queue_family(VkPhysicalDevice adapter) -> Optional<usize> {
  uint32_t num_queues = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(adapter, &num_queues, nullptr);
  SmallVector<VkQueueFamilyProperties, 4> queues(num_queues);
  vkGetPhysicalDeviceQueueFamilyProperties(adapter, &num_queues, queues.data());
  for (usize i = 0; i < num_queues; ++i) {
    if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      return i;
    }
  }
  return None;
}

} // namespace

void Renderer::create_device() {
  u32 num_supported_extensions;
  vkEnumerateDeviceExtensionProperties(m_adapter, nullptr,
                                       &num_supported_extensions, nullptr);
  Vector<VkExtensionProperties> supported_extensions(num_supported_extensions);
  vkEnumerateDeviceExtensionProperties(m_adapter, nullptr,
                                       &num_supported_extensions,
                                       supported_extensions.data());

  auto is_extension_supported = [&](const char *ext) {
    return std::ranges::find_if(supported_extensions,
                                [&](const VkExtensionProperties &ext_props) {
                                  return std::strcmp(
                                             ext, ext_props.extensionName) == 0;
                                }) != supported_extensions.end();
  };

  std::array required_extensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME,
  };

  std::array optional_extensions = {
      VK_AMD_ANTI_LAG_EXTENSION_NAME,
  };

  Vector<const char *> extensions;
  extensions.reserve(required_extensions.size() + optional_extensions.size());
  extensions = required_extensions;
  for (const char *ext : optional_extensions) {
    if (is_extension_supported(ext)) {
      fmt::println("Found optional extension {}", ext);
      extensions.push_back(ext);
    }
  }

  void *pnext = nullptr;
  auto add_features = [&](auto &features) {
    ren_assert(!features.pNext);
    features.pNext = pnext;
    pnext = &features;
  };

  // Query optional feature support.

  VkPhysicalDeviceAntiLagFeaturesAMD amd_anti_lag_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ANTI_LAG_FEATURES_AMD,
  };

  add_features(amd_anti_lag_features);

  {
    VkPhysicalDeviceFeatures2 features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = pnext,
    };
    vkGetPhysicalDeviceFeatures2(m_adapter, &features);
  }

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
      .vulkanMemoryModelDeviceScope = true,
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

  // Add supported optional features.

  if (amd_anti_lag_features.antiLag) {
    fmt::println("Enable AMD Anti-Lag feature");
    add_features(amd_anti_lag_features);
    m_features.set((usize)RendererFeature::AmdAntiLag);
  }

  float queue_priority = 1.0f;
  VkDeviceQueueCreateInfo queue_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = m_graphics_queue_family,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
  };

  VkDeviceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = pnext,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };

  throw_if_failed(vkCreateDevice(m_adapter, &create_info, nullptr, &m_device),
                  "Vulkan: Failed to create device");
}

namespace {

auto create_allocator(VkInstance instance, VkPhysicalDevice adapter,
                      VkDevice device) -> VmaAllocator {
  VmaVulkanFunctions vk = {
      .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
      .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
      .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
      .vkGetPhysicalDeviceMemoryProperties =
          vkGetPhysicalDeviceMemoryProperties,
      .vkAllocateMemory = vkAllocateMemory,
      .vkFreeMemory = vkFreeMemory,
      .vkMapMemory = vkMapMemory,
      .vkUnmapMemory = vkUnmapMemory,
      .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
      .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
      .vkBindBufferMemory = vkBindBufferMemory,
      .vkBindImageMemory = vkBindImageMemory,
      .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
      .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
      .vkCreateBuffer = vkCreateBuffer,
      .vkDestroyBuffer = vkDestroyBuffer,
      .vkCreateImage = vkCreateImage,
      .vkDestroyImage = vkDestroyImage,
      .vkCmdCopyBuffer = vkCmdCopyBuffer,
      .vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2,
      .vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2,
      .vkBindBufferMemory2KHR = vkBindBufferMemory2,
      .vkBindImageMemory2KHR = vkBindImageMemory2,
      .vkGetPhysicalDeviceMemoryProperties2KHR =
          vkGetPhysicalDeviceMemoryProperties2,
      .vkGetDeviceBufferMemoryRequirements =
          vkGetDeviceBufferMemoryRequirements,
      .vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements,
  };

  VmaAllocatorCreateInfo allocator_info = {
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = adapter,
      .device = device,
      .pVulkanFunctions = &vk,
      .instance = instance,
      .vulkanApiVersion = VK_API_VERSION_1_3,
  };

  VmaAllocator allocator;
  throw_if_failed(vmaCreateAllocator(&allocator_info, &allocator),
                  "VMA: Failed to create allocator");

  return allocator;
}

} // namespace

Renderer::Renderer(Span<const char *const> extensions, u32 adapter) {
  throw_if_failed(volkInitialize(), "Volk: failed to initialize");

  m_instance = create_instance(extensions);

  volkLoadInstanceOnly(get_instance());

#if REN_VULKAN_VALIDATION
  m_debug_callback = create_debug_report_callback(m_instance);
#endif

  m_adapter = find_adapter(get_instance(), adapter);
  if (m_adapter) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_adapter, &props);
    fmt::println("Running on {}", props.deviceName);
  } else {
    throw std::runtime_error("Vulkan: Failed to find requested adapter");
  }

  Optional<usize> graphics_queue_family = find_graphics_queue_family(m_adapter);
  if (graphics_queue_family) {
    m_graphics_queue_family = *graphics_queue_family;
  } else {
    throw std::runtime_error("Vulkan: Failed to find graphics queue");
  }

  create_device();

  volkLoadDevice(get_device());

  vkGetDeviceQueue(get_device(), m_graphics_queue_family, 0, &m_graphics_queue);

  m_allocator = create_allocator(get_instance(), m_adapter, get_device());
}

Renderer::~Renderer() {
  wait_idle();
  vmaDestroyAllocator(m_allocator);
  vkDestroyDevice(m_device, nullptr);
#if REN_VULKAN_VALIDATION
  vkDestroyDebugReportCallbackEXT(m_instance, m_debug_callback, nullptr);
#endif
  vkDestroyInstance(m_instance, nullptr);
}

auto Renderer::create_scene(ISwapchain &swapchain)
    -> expected<std::unique_ptr<IScene>> {
  return std::make_unique<Scene>(*this, static_cast<Swapchain &>(swapchain));
}

void Renderer::wait_idle() {
  throw_if_failed(vkDeviceWaitIdle(get_device()),
                  "Vulkan: Failed to wait for idle device");
}

auto Renderer::create_descriptor_pool(
    const DescriptorPoolCreateInfo &&create_info) -> Handle<DescriptorPool> {
  StaticVector<VkDescriptorPoolSize, MAX_DESCIPTOR_BINDINGS> pool_sizes;

  for (int i = 0; i < DESCRIPTOR_TYPE_COUNT; ++i) {
    auto type = static_cast<VkDescriptorType>(i);
    auto count = create_info.pool_sizes[type];
    if (count > 0) {
      pool_sizes.push_back({
          .type = type,
          .descriptorCount = count,
      });
    }
  }

  VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = create_info.flags,
      .maxSets = create_info.set_count,
      .poolSizeCount = unsigned(pool_sizes.size()),
      .pPoolSizes = pool_sizes.data(),
  };

  VkDescriptorPool pool;
  throw_if_failed(
      vkCreateDescriptorPool(get_device(), &pool_info, nullptr, &pool),
      "Vulkan: Failed to create descriptor pool");
  set_debug_name(get_device(), pool, create_info.name);

  return (m_descriptor_pools.emplace(DescriptorPool{
      .handle = pool,
      .flags = create_info.flags,
      .set_count = create_info.set_count,
      .pool_sizes = create_info.pool_sizes,
  }));
}

void Renderer::destroy(Handle<DescriptorPool> pool) {
  m_descriptor_pools.try_pop(pool).map([&](const DescriptorPool &pool) {
    vkDestroyDescriptorPool(m_device, pool.handle, nullptr);
  });
}

auto Renderer::try_get_descriptor_pool(Handle<DescriptorPool> pool) const
    -> Optional<const DescriptorPool &> {
  return m_descriptor_pools.try_get(pool);
}

auto Renderer::get_descriptor_pool(Handle<DescriptorPool> pool) const
    -> const DescriptorPool & {
  ren_assert(m_descriptor_pools.contains(pool));
  return m_descriptor_pools[pool];
}

void Renderer::reset_descriptor_pool(Handle<DescriptorPool> pool) const {
  throw_if_failed(
      vkResetDescriptorPool(get_device(), get_descriptor_pool(pool).handle, 0),
      "Vulkan: Failed to reset descriptor pool");
}

auto Renderer::create_descriptor_set_layout(
    const DescriptorSetLayoutCreateInfo &&create_info)
    -> Handle<DescriptorSetLayout> {

  StaticVector<VkDescriptorBindingFlags, MAX_DESCIPTOR_BINDINGS> binding_flags;
  StaticVector<VkDescriptorSetLayoutBinding, MAX_DESCIPTOR_BINDINGS> bindings;

  for (const auto &[index, binding] :
       create_info.bindings | std::views::enumerate) {
    if (binding.count == 0) {
      continue;
    }
    binding_flags.push_back(binding.flags);
    bindings.push_back({
        .binding = unsigned(index),
        .descriptorType = binding.type,
        .descriptorCount = binding.count,
        .stageFlags = binding.stages,
    });
  }

  VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info = {
      .sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = unsigned(binding_flags.size()),
      .pBindingFlags = binding_flags.data(),
  };

  VkDescriptorSetLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = &binding_flags_info,
      .flags = create_info.flags,
      .bindingCount = unsigned(bindings.size()),
      .pBindings = bindings.data(),
  };

  VkDescriptorSetLayout layout;
  throw_if_failed(
      vkCreateDescriptorSetLayout(get_device(), &layout_info, nullptr, &layout),
      "Vulkann: Failed to create descriptor set layout");
  set_debug_name(get_device(), layout, create_info.name);

  return m_descriptor_set_layouts.emplace(DescriptorSetLayout{
      .handle = layout,
      .flags = create_info.flags,
      .bindings = create_info.bindings,
  });
}

void Renderer::destroy(Handle<DescriptorSetLayout> layout) {
  m_descriptor_set_layouts.try_pop(layout).map(
      [&](const DescriptorSetLayout &layout) {
        vkDestroyDescriptorSetLayout(m_device, layout.handle, nullptr);
      });
}

auto Renderer::try_get_descriptor_set_layout(Handle<DescriptorSetLayout> layout)
    const -> Optional<const DescriptorSetLayout &> {
  return m_descriptor_set_layouts.try_get(layout);
}

auto Renderer::get_descriptor_set_layout(
    Handle<DescriptorSetLayout> layout) const -> const DescriptorSetLayout & {
  ren_assert(m_descriptor_set_layouts.contains(layout));
  return m_descriptor_set_layouts[layout];
}

auto Renderer::allocate_descriptor_sets(
    Handle<DescriptorPool> pool,
    TempSpan<const Handle<DescriptorSetLayout>> layouts,
    VkDescriptorSet *sets) const -> bool {
  auto vk_layouts = layouts | map([&](Handle<DescriptorSetLayout> layout) {
                      return get_descriptor_set_layout(layout).handle;
                    }) |
                    std::ranges::to<SmallVector<VkDescriptorSetLayout>>();

  VkDescriptorSetAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = get_descriptor_pool(pool).handle,
      .descriptorSetCount = unsigned(vk_layouts.size()),
      .pSetLayouts = vk_layouts.data(),
  };

  auto result = vkAllocateDescriptorSets(get_device(), &alloc_info, sets);
  switch (result) {
  default: {
    throw_if_failed(result, "Vulkan: Failed to allocate descriptor sets");
  }
  case VK_SUCCESS: {
    return true;
  }
  case VK_ERROR_FRAGMENTED_POOL:
  case VK_ERROR_OUT_OF_POOL_MEMORY: {
    return false;
  }
  }
}

auto Renderer::allocate_descriptor_set(Handle<DescriptorPool> pool,
                                       Handle<DescriptorSetLayout> layout) const
    -> Optional<VkDescriptorSet> {
  VkDescriptorSet set;
  if (allocate_descriptor_sets(pool, {&layout, 1}, &set)) {
    return set;
  }
  return None;
}

void Renderer::write_descriptor_sets(
    TempSpan<const VkWriteDescriptorSet> configs) const {
  vkUpdateDescriptorSets(get_device(), configs.size(), configs.data(), 0,
                         nullptr);
}

auto Renderer::create_buffer(const BufferCreateInfo &&create_info)
    -> Handle<Buffer> {
  ren_assert(create_info.size > 0);

  VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = create_info.size,
      .usage = create_info.usage,
  };

  VmaAllocationCreateInfo alloc_info = {
      .usage = VMA_MEMORY_USAGE_AUTO,
  };

  switch (create_info.heap) {
    using enum BufferHeap;
  default:
    unreachable("Unknown BufferHeap");
  case Static:
    break;
  case Dynamic: {
    alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  } break;
  case Staging: {
    buffer_info.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
  } break;
  case Readback: {
    buffer_info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                       VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
  } break;
  };

  VkBuffer buffer;
  VmaAllocation allocation;
  VmaAllocationInfo map_info;
  throw_if_failed(vmaCreateBuffer(get_allocator(), &buffer_info, &alloc_info,
                                  &buffer, &allocation, &map_info),
                  "VMA: Failed to create buffer");
  set_debug_name(get_device(), buffer, create_info.name);

  uint64_t address = 0;
  if (create_info.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
    VkBufferDeviceAddressInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer,
    };
    address = vkGetBufferDeviceAddress(get_device(), &buffer_info);
  }

  return m_buffers.emplace(Buffer{
      .handle = buffer,
      .allocation = allocation,
      .ptr = (std::byte *)map_info.pMappedData,
      .address = address,
      .size = create_info.size,
      .heap = create_info.heap,
      .usage = create_info.usage,
  });
}

void Renderer::destroy(Handle<Buffer> handle) {
  m_buffers.try_pop(handle).map([&](const Buffer &buffer) {
    vmaDestroyBuffer(m_allocator, buffer.handle, buffer.allocation);
  });
}

auto Renderer::try_get_buffer(Handle<Buffer> buffer) const
    -> Optional<const Buffer &> {
  return m_buffers.try_get(buffer);
};

auto Renderer::get_buffer(Handle<Buffer> buffer) const -> const Buffer & {
  ren_assert(m_buffers.contains(buffer));
  return m_buffers[buffer];
};

auto Renderer::try_get_buffer_view(Handle<Buffer> handle) const
    -> Optional<BufferView> {
  return try_get_buffer(handle).map([&](const Buffer &buffer) -> BufferView {
    return {
        .buffer = handle,
        .count = buffer.size,
    };
  });
};

auto Renderer::get_buffer_view(Handle<Buffer> handle) const -> BufferView {
  const auto &buffer = get_buffer(handle);
  return {
      .buffer = handle,
      .count = buffer.size,
  };
};

auto Renderer::create_texture(const TextureCreateInfo &&create_info)
    -> Handle<Texture> {
  ren_assert(create_info.width > 0);
  ren_assert(create_info.height > 0);
  ren_assert(create_info.depth > 0);
  ren_assert(create_info.num_mip_levels > 0);
  ren_assert(create_info.num_array_layers > 0);

  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = create_info.type,
      .format = (VkFormat)TinyImageFormat_ToVkFormat(create_info.format),
      .extent = {create_info.width, create_info.height, create_info.depth},
      .mipLevels = create_info.num_mip_levels,
      .arrayLayers = create_info.num_array_layers,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = create_info.usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO};

  VkImage image;
  VmaAllocation allocation;
  throw_if_failed(vmaCreateImage(get_allocator(), &image_info, &alloc_info,
                                 &image, &allocation, nullptr),
                  "VMA: Failed to create image");
  set_debug_name(get_device(), image, create_info.name);

  return m_textures.emplace(Texture{
      .image = image,
      .allocation = allocation,
      .type = create_info.type,
      .format = create_info.format,
      .usage = create_info.usage,
      .width = create_info.width,
      .height = create_info.height,
      .depth = create_info.depth,
      .num_mip_levels = create_info.num_mip_levels,
      .num_array_layers = create_info.num_array_layers,
  });
}

auto Renderer::create_swapchain_texture(
    const SwapchainTextureCreateInfo &&create_info) -> Handle<Texture> {
  set_debug_name(get_device(), create_info.image, "Swapchain image");

  return m_textures.emplace(Texture{
      .image = create_info.image,
      .type = VK_IMAGE_TYPE_2D,
      .format = create_info.format,
      .usage = create_info.usage,
      .width = create_info.width,
      .height = create_info.height,
      .depth = 1,
      .num_mip_levels = 1,
      .num_array_layers = 1,
  });
}

void Renderer::destroy(Handle<Texture> handle) {
  m_textures.try_pop(handle).map([&](const Texture &texture) {
    if (texture.allocation) {
      vmaDestroyImage(m_allocator, texture.image, texture.allocation);
    }
    for (const auto &[_, view] : m_image_views[handle]) {
      vkDestroyImageView(m_device, view, nullptr);
    }
    m_image_views.erase(handle);
  });
}

auto Renderer::try_get_texture(Handle<Texture> texture) const
    -> Optional<const Texture &> {
  return m_textures.try_get(texture);
}

auto Renderer::get_texture(Handle<Texture> texture) const -> const Texture & {
  ren_assert(m_textures.contains(texture));
  return m_textures[texture];
}

static auto get_texture_default_view_type(VkImageType type,
                                          u16 num_array_layers)
    -> VkImageViewType {
  if (num_array_layers > 1) {
    switch (type) {
    default:
      break;
    case VK_IMAGE_TYPE_1D:
      return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case VK_IMAGE_TYPE_2D:
      return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    }
  } else {
    switch (type) {
    default:
      break;
    case VK_IMAGE_TYPE_1D:
      return VK_IMAGE_VIEW_TYPE_1D;
    case VK_IMAGE_TYPE_2D:
      return VK_IMAGE_VIEW_TYPE_2D;
    case VK_IMAGE_TYPE_3D:
      return VK_IMAGE_VIEW_TYPE_3D;
    }
  }
  unreachable("Invalid VkImageType/num_array_layers combination:", int(type),
              num_array_layers);
}

auto Renderer::try_get_texture_view(Handle<Texture> handle) const
    -> Optional<TextureView> {
  return try_get_texture(handle).map(
      [&](const Texture &texture) -> TextureView {
        return {
            .texture = handle,
            .type = get_texture_default_view_type(texture.type,
                                                  texture.num_array_layers),
            .format = texture.format,
            .num_mip_levels = texture.num_mip_levels,
            .num_array_layers = texture.num_array_layers,
        };
      });
}

auto Renderer::get_texture_view(Handle<Texture> handle) const -> TextureView {
  const auto &texture = get_texture(handle);
  return {
      .texture = handle,
      .type =
          get_texture_default_view_type(texture.type, texture.num_array_layers),
      .format = texture.format,
      .num_mip_levels = texture.num_mip_levels,
      .num_array_layers = texture.num_array_layers,
  };
}

auto Renderer::get_texture_view_size(const TextureView &view,
                                     u32 mip_level_offset) const -> glm::uvec3 {
  const Texture &texture = get_texture(view.texture);
  ren_assert(view.first_mip_level + mip_level_offset < texture.num_mip_levels);
  return get_size_at_mip_level(texture.size,
                               view.first_mip_level + mip_level_offset);
}

auto Renderer::getVkImageView(const TextureView &view) -> VkImageView {
  auto &image_views = m_image_views[view.texture];

  [[likely]] if (Optional<VkImageView> image_view = image_views.get(view)) {
    return *image_view;
  }

  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = get_texture(view.texture).image,
      .viewType = view.type,
      .format = (VkFormat)TinyImageFormat_ToVkFormat(view.format),
      .components =
          {
              .r = view.swizzle.r,
              .g = view.swizzle.g,
              .b = view.swizzle.b,
              .a = view.swizzle.a,
          },
      .subresourceRange =
          {
              .aspectMask = getVkImageAspectFlags(view.format),
              .baseMipLevel = view.first_mip_level,
              .levelCount = view.num_mip_levels,
              .baseArrayLayer = view.first_array_layer,
              .layerCount = view.num_array_layers,
          },
  };
  VkImageView image_view;
  throw_if_failed(
      vkCreateImageView(get_device(), &view_info, nullptr, &image_view),
      "Vulkan: Failed to create image view");

  image_views.insert(view, image_view);

  return image_view;
}

static constexpr auto REDUCTION_MODE_MAP = [] {
  using enum SamplerReductionMode;
  std::array<VkSamplerReductionMode, (int)Last + 1> map = {};
  map[(int)WeightedAverage] = VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
  map[(int)Min] = VK_SAMPLER_REDUCTION_MODE_MIN;
  map[(int)Max] = VK_SAMPLER_REDUCTION_MODE_MAX;
  return map;
}();

auto Renderer::create_sampler(const SamplerCreateInfo &&create_info)
    -> Handle<Sampler> {

  VkSamplerReductionModeCreateInfo reduction_mode_info = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
      .reductionMode = REDUCTION_MODE_MAP[(int)create_info.reduction_mode],
  };

  VkSamplerCreateInfo sampler_info = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .pNext = &reduction_mode_info,
      .magFilter = create_info.mag_filter,
      .minFilter = create_info.min_filter,
      .mipmapMode = create_info.mipmap_mode,
      .addressModeU = create_info.address_mode_u,
      .addressModeV = create_info.address_mode_v,
      .anisotropyEnable = create_info.anisotropy > 0.0f,
      .maxAnisotropy = create_info.anisotropy,
      .maxLod = VK_LOD_CLAMP_NONE,
  };

  VkSampler sampler;
  throw_if_failed(
      vkCreateSampler(get_device(), &sampler_info, nullptr, &sampler),
      "Vulkan: Failed to create sampler");
  set_debug_name(get_device(), sampler, create_info.name);

  return m_samplers.emplace(Sampler{
      .handle = sampler,
      .mag_filter = create_info.mag_filter,
      .min_filter = create_info.min_filter,
      .mipmap_mode = create_info.mipmap_mode,
      .address_mode_u = create_info.address_mode_u,
      .address_mode_v = create_info.address_mode_v,
  });
}

void Renderer::destroy(Handle<Sampler> sampler) {
  m_samplers.try_pop(sampler).map([&](const Sampler &sampler) {
    vkDestroySampler(m_device, sampler.handle, nullptr);
  });
}

auto Renderer::get_sampler(Handle<Sampler> sampler) const -> const Sampler & {
  ren_assert(m_samplers.contains(sampler));
  return m_samplers[sampler];
}

auto Renderer::create_semaphore(const SemaphoreCreateInfo &&create_info)
    -> Handle<Semaphore> {
  VkSemaphoreTypeCreateInfo semaphore_type_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
  };
  create_info.initial_value.map([&](u64 initial_value) {
    semaphore_type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    semaphore_type_info.initialValue = initial_value;
  });
  VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &semaphore_type_info,
  };

  VkSemaphore semaphore;
  throw_if_failed(
      vkCreateSemaphore(get_device(), &semaphore_info, nullptr, &semaphore),
      "Vulkan: Failed to create semaphore");
  set_debug_name(get_device(), semaphore, create_info.name);

  return m_semaphores.emplace(Semaphore{.handle = semaphore});
}

void Renderer::destroy(Handle<Semaphore> semaphore) {
  m_semaphores.try_pop(semaphore).map([&](const Semaphore &semaphore) {
    vkDestroySemaphore(m_device, semaphore.handle, nullptr);
  });
}

auto Renderer::wait_for_semaphore(const Semaphore &semaphore, uint64_t value,
                                  std::chrono::nanoseconds timeout) const
    -> VkResult {
  VkSemaphoreWaitInfo wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &semaphore.handle,
      .pValues = &value,
  };
  auto result = vkWaitSemaphores(get_device(), &wait_info, timeout.count());
  switch (result) {
  case VK_SUCCESS:
  case VK_TIMEOUT:
    return result;
  default:
    throw std::runtime_error{"Vulkan: Failed to wait for semaphore"};
  };
}

void Renderer::wait_for_semaphore(const Semaphore &semaphore,
                                  uint64_t value) const {
  auto result = wait_for_semaphore(semaphore, value,
                                   std::chrono::nanoseconds(UINT64_MAX));
  ren_assert(result == VK_SUCCESS);
}

auto Renderer::try_get_semaphore(Handle<Semaphore> semaphore) const
    -> Optional<const Semaphore &> {
  return m_semaphores.try_get(semaphore);
}

auto Renderer::get_semaphore(Handle<Semaphore> semaphore) const
    -> const Semaphore & {
  ren_assert(m_semaphores.contains(semaphore));
  return m_semaphores[semaphore];
}

void Renderer::queueSubmit(
    VkQueue queue, TempSpan<const VkCommandBufferSubmitInfo> cmd_buffers,
    TempSpan<const VkSemaphoreSubmitInfo> wait_semaphores,
    TempSpan<const VkSemaphoreSubmitInfo> signal_semaphores) {
  ren_prof_zone("Renderer::queueSubmit");
  VkSubmitInfo2 submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .waitSemaphoreInfoCount = uint32_t(wait_semaphores.size()),
      .pWaitSemaphoreInfos = wait_semaphores.data(),
      .commandBufferInfoCount = uint32_t(cmd_buffers.size()),
      .pCommandBufferInfos = cmd_buffers.data(),
      .signalSemaphoreInfoCount = uint32_t(signal_semaphores.size()),
      .pSignalSemaphoreInfos = signal_semaphores.data(),
  };
  throw_if_failed(vkQueueSubmit2(queue, 1, &submit_info, nullptr),
                  "Vulkan: Failed to submit work to queue");
}

static auto create_shader_module(VkDevice device,
                                 std::span<const std::byte> code) {
  ren_assert(code.size() % sizeof(u32) == 0);
  VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code.size(),
      .pCode = reinterpret_cast<const u32 *>(code.data()),
  };
  VkShaderModule module;
  throw_if_failed(vkCreateShaderModule(device, &module_info, nullptr, &module),
                  "Vulkan: Failed to create shader module");
  return module;
}

auto Renderer::create_graphics_pipeline(
    const GraphicsPipelineCreateInfo &&create_info)
    -> Handle<GraphicsPipeline> {
  constexpr size_t MAX_GRAPHICS_SHADER_STAGES = 2;

  StaticVector<VkShaderModule, MAX_GRAPHICS_SHADER_STAGES> shader_modules;
  StaticVector<Vector<u32>, MAX_GRAPHICS_SHADER_STAGES> spec_data;
  StaticVector<Vector<VkSpecializationMapEntry>, MAX_GRAPHICS_SHADER_STAGES>
      spec_map;
  StaticVector<VkSpecializationInfo, MAX_GRAPHICS_SHADER_STAGES> spec_infos;
  StaticVector<VkPipelineShaderStageCreateInfo, MAX_GRAPHICS_SHADER_STAGES>
      shaders;

  StaticVector<VkDynamicState, 3> dynamic_states = {
      VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
      VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
  };

  VkShaderStageFlags stages = 0;

  auto add_shader = [&](VkShaderStageFlagBits stage, const ShaderInfo &shader) {
    auto module = create_shader_module(get_device(), shader.code);

    Vector<u32> &data = spec_data.emplace_back();
    Vector<VkSpecializationMapEntry> &map = spec_map.emplace_back();
    data.reserve(shader.spec_constants.size());
    map.reserve(shader.spec_constants.size());
    for (const SpecConstant &c : shader.spec_constants) {
      map.push_back({
          .constantID = c.id,
          .offset = u32(data.size() * sizeof(c.value)),
          .size = sizeof(c.value),
      });
      data.push_back(c.value);
    }
    VkSpecializationInfo &spec_info = spec_infos.emplace_back();
    spec_info = {
        .mapEntryCount = u32(map.size()),
        .pMapEntries = map.data(),
        .dataSize = Span(data).size_bytes(),
        .pData = data.data(),
    };

    shaders.push_back({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage,
        .module = module,
        .pName = shader.entry_point,
        .pSpecializationInfo = &spec_info,
    });
    shader_modules.push_back(module);
    stages |= stage;
  };

  add_shader(VK_SHADER_STAGE_VERTEX_BIT, create_info.vertex_shader);
  create_info.fragment_shader.map([&](const ShaderInfo &shader) {
    add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, shader);
  });

  auto color_attachment_formats =
      create_info.color_attachments |
      map([&](const ColorAttachmentInfo &attachment) {
        return (VkFormat)TinyImageFormat_ToVkFormat(attachment.format);
      }) |
      std::ranges::to<StaticVector<VkFormat, MAX_COLOR_ATTACHMENTS>>();

  VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = u32(color_attachment_formats.size()),
      .pColorAttachmentFormats = color_attachment_formats.data(),
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = create_info.input_assembly.topology,
  };

  VkPipelineViewportStateCreateInfo viewport_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .cullMode = (VkCullModeFlags)create_info.rasterization.cull_mode,
      .frontFace = create_info.rasterization.front_face,
      .lineWidth = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples =
          static_cast<VkSampleCountFlagBits>(create_info.multisample.samples),
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
  };

  create_info.depth_test.map([&](const DepthTestInfo &depth_test) {
    rendering_info.depthAttachmentFormat =
        (VkFormat)TinyImageFormat_ToVkFormat(depth_test.format);
    depth_stencil_info.depthTestEnable = true;
    depth_stencil_info.depthWriteEnable = depth_test.write_depth;
    depth_test.compare_op.visit(OverloadSet{
        [&](DynamicState) {
          dynamic_states.push_back(VK_DYNAMIC_STATE_DEPTH_COMPARE_OP);
        },
        [&](VkCompareOp op) { depth_stencil_info.depthCompareOp = op; },
    });
  });

  auto color_attachments =
      create_info.color_attachments |
      map([&](const ColorAttachmentInfo &attachment) {
        return attachment.blending.map_or(
            [&](const ColorBlendAttachmentInfo &blending)
                -> VkPipelineColorBlendAttachmentState {
              return {
                  .blendEnable = true,
                  .srcColorBlendFactor = blending.src_color_blend_factor,
                  .dstColorBlendFactor = blending.dst_color_blend_factor,
                  .colorBlendOp = blending.color_blend_op,
                  .srcAlphaBlendFactor = blending.src_alpha_blend_factor,
                  .dstAlphaBlendFactor = blending.dst_alpha_blend_factor,
                  .alphaBlendOp = blending.alpha_blend_op,
                  .colorWriteMask = attachment.write_mask,
              };
            },
            VkPipelineColorBlendAttachmentState{
                .colorWriteMask = attachment.write_mask,
            });
      }) |
      std::ranges::to<StaticVector<VkPipelineColorBlendAttachmentState,
                                   MAX_COLOR_ATTACHMENTS>>();

  VkPipelineColorBlendStateCreateInfo blend_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = u32(color_attachments.size()),
      .pAttachments = color_attachments.data(),
  };

  VkPipelineDynamicStateCreateInfo dynamic_state_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = u32(dynamic_states.size()),
      .pDynamicStates = dynamic_states.data(),
  };

  VkGraphicsPipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = u32(shaders.size()),
      .pStages = shaders.data(),
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &input_assembly_info,
      .pViewportState = &viewport_info,
      .pRasterizationState = &rasterization_info,
      .pMultisampleState = &multisample_info,
      .pDepthStencilState = &depth_stencil_info,
      .pColorBlendState = &blend_info,
      .pDynamicState = &dynamic_state_info,
      .layout = get_pipeline_layout(create_info.layout).handle,
  };

  VkPipeline pipeline;
  throw_if_failed(vkCreateGraphicsPipelines(get_device(), nullptr, 1,
                                            &pipeline_info, nullptr, &pipeline),
                  "Vulkan: Failed to create graphics pipeline");
  set_debug_name(get_device(), pipeline, create_info.name);
  for (VkShaderModule module : shader_modules) {
    vkDestroyShaderModule(get_device(), module, nullptr);
  }

  return m_graphics_pipelines.emplace(GraphicsPipeline{
      .handle = pipeline,
      .layout = create_info.layout,
      .stages = stages,
      .input_assembly = create_info.input_assembly,
      .multisample = create_info.multisample,
      .depth_test = create_info.depth_test,
      .color_attachments = create_info.color_attachments,
  });
}

void Renderer::destroy(Handle<GraphicsPipeline> pipeline) {
  m_graphics_pipelines.try_pop(pipeline).map(
      [&](const GraphicsPipeline &pipeline) {
        vkDestroyPipeline(m_device, pipeline.handle, nullptr);
      });
}

auto Renderer::try_get_graphics_pipeline(Handle<GraphicsPipeline> pipeline)
    const -> Optional<const GraphicsPipeline &> {
  return m_graphics_pipelines.try_get(pipeline);
}

auto Renderer::get_graphics_pipeline(Handle<GraphicsPipeline> pipeline) const
    -> const GraphicsPipeline & {
  ren_assert(m_graphics_pipelines.contains(pipeline));
  return m_graphics_pipelines[pipeline];
}

auto Renderer::create_compute_pipeline(
    const ComputePipelineCreateInfo &&create_info) -> Handle<ComputePipeline> {
  VkShaderModule module =
      create_shader_module(get_device(), create_info.shader.code);

  VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
          {
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_COMPUTE_BIT,
              .module = module,
              .pName = create_info.shader.entry_point,
          },
      .layout = get_pipeline_layout(create_info.layout).handle,
  };

  VkPipeline pipeline;
  throw_if_failed(vkCreateComputePipelines(get_device(), nullptr, 1,
                                           &pipeline_info, nullptr, &pipeline),
                  "Vulkan: Failed to create compute pipeline");
  set_debug_name(get_device(), pipeline, create_info.name);
  vkDestroyShaderModule(get_device(), module, nullptr);

  return m_compute_pipelines.emplace(ComputePipeline{
      .handle = pipeline,
      .layout = create_info.layout,
  });
}

void Renderer::destroy(Handle<ComputePipeline> pipeline) {
  m_compute_pipelines.try_pop(pipeline).map(
      [&](const ComputePipeline &pipeline) {
        vkDestroyPipeline(m_device, pipeline.handle, nullptr);
      });
}

auto Renderer::try_get_compute_pipeline(Handle<ComputePipeline> pipeline) const
    -> Optional<const ComputePipeline &> {
  return m_compute_pipelines.try_get(pipeline);
}

auto Renderer::get_compute_pipeline(Handle<ComputePipeline> pipeline) const
    -> const ComputePipeline & {
  ren_assert(m_compute_pipelines.contains(pipeline));
  return m_compute_pipelines[pipeline];
}

auto Renderer::create_pipeline_layout(
    const PipelineLayoutCreateInfo &&create_info) -> Handle<PipelineLayout> {
  auto layouts =
      create_info.set_layouts | map([&](Handle<DescriptorSetLayout> layout) {
        return get_descriptor_set_layout(layout).handle;
      }) |
      std::ranges::to<
          StaticVector<VkDescriptorSetLayout, MAX_DESCRIPTOR_SETS>>();

  VkPipelineLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = unsigned(layouts.size()),
      .pSetLayouts = layouts.data(),
      .pushConstantRangeCount = create_info.push_constants.size > 0 ? 1u : 0u,
      .pPushConstantRanges = &create_info.push_constants,
  };

  VkPipelineLayout layout;
  throw_if_failed(
      vkCreatePipelineLayout(get_device(), &layout_info, nullptr, &layout),
      "Vulkan: Failed to create pipeline layout");
  set_debug_name(get_device(), layout, create_info.name);

  return m_pipeline_layouts.emplace(PipelineLayout{
      .handle = layout,
      .set_layouts{create_info.set_layouts},
      .push_constants = create_info.push_constants,
  });
}

void Renderer::destroy(Handle<PipelineLayout> layout) {
  m_pipeline_layouts.try_pop(layout).map([&](const PipelineLayout &layout) {
    vkDestroyPipelineLayout(m_device, layout.handle, nullptr);
  });
}

auto Renderer::try_get_pipeline_layout(Handle<PipelineLayout> layout) const
    -> Optional<const PipelineLayout &> {
  return m_pipeline_layouts.try_get(layout);
}

auto Renderer::get_pipeline_layout(Handle<PipelineLayout> layout) const
    -> const PipelineLayout & {
  ren_assert(m_pipeline_layouts.contains(layout));
  return m_pipeline_layouts[layout];
}

bool Renderer::is_feature_supported(RendererFeature feature) const {
  auto i = (usize)feature;
  ren_assert(i <= (usize)RendererFeature::Last);
  return m_features[i];
}

auto Renderer::queue_present(const VkPresentInfoKHR &present_info) -> VkResult {
  return vkQueuePresentKHR(getGraphicsQueue(), &present_info);
}

void Renderer::amd_anti_lag(u64 frame, VkAntiLagStageAMD stage, u32 max_fps,
                            VkAntiLagModeAMD mode) {
  ren_prof_zone("AMD Anti-Lag");
  VkAntiLagPresentationInfoAMD present_info = {
      .sType = VK_STRUCTURE_TYPE_ANTI_LAG_PRESENTATION_INFO_AMD,
      .stage = stage,
      .frameIndex = frame,
  };
  VkAntiLagDataAMD anti_lag_data = {
      .sType = VK_STRUCTURE_TYPE_ANTI_LAG_DATA_AMD,
      .mode = mode,
      .maxFPS = max_fps,
      .pPresentationInfo = &present_info,
  };
  vkAntiLagUpdateAMD(m_device, &anti_lag_data);
}

} // namespace ren
