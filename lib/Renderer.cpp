#include "Renderer.hpp"
#include "Formats.hpp"
#include "Scene.hpp"
#include "Support/Array.hpp"
#include "Support/Errors.hpp"
#include "Support/Views.hpp"
#include "Swapchain.hpp"

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

auto Renderer::getRequiredLayers() noexcept -> Span<const char *const> {
  static constexpr auto layers = makeArray<const char *>(
#if REN_VULKAN_VALIDATION
      "VK_LAYER_KHRONOS_validation"
#endif
  );
  return layers;
}

auto Renderer::getInstanceExtensions() noexcept -> Span<const char *const> {
  static constexpr auto extensions = makeArray<const char *>(
#if REN_DEBUG_NAMES
      VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
  );
  return extensions;
}

namespace {

auto create_instance(Span<const char *const> external_extensions)
    -> UniqueInstance {
  VkApplicationInfo application_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .apiVersion = Renderer::getRequiredAPIVersion(),
  };

  auto layers = Renderer::getRequiredLayers();

  SmallVector<const char *> extensions(external_extensions);
  extensions.append(Renderer::getInstanceExtensions());

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

  return UniqueInstance(instance);
}

auto find_adapter(VkInstance instance, u32 adapter) -> VkPhysicalDevice {
  uint32_t num_adapters = 0;
  throw_if_failed(vkEnumeratePhysicalDevices(instance, &num_adapters, nullptr),
                  "Vulkan: Failed to enumerate physical device");
  SmallVector<VkPhysicalDevice> adapters(num_adapters);
  throw_if_failed(
      vkEnumeratePhysicalDevices(instance, &num_adapters, adapters.data()),
      "Vulkan: Failed to enumerate physical device");
  adapters.resize(num_adapters);
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

auto create_device(VkPhysicalDevice adapter, u32 graphics_queue_family)
    -> UniqueDevice {
  float queue_priority = 1.0f;
  VkDeviceQueueCreateInfo queue_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = graphics_queue_family,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
  };

  VkPhysicalDeviceFeatures2 vulkan10_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .features = {
          .samplerAnisotropy = true,
          .shaderInt64 = true,
          .shaderInt16 = true,
      }};

  VkPhysicalDeviceVulkan11Features vulkan11_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .pNext = &vulkan10_features,
      .storageBuffer16BitAccess = true,
      .shaderDrawParameters = true,
  };

  VkPhysicalDeviceVulkan12Features vulkan12_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &vulkan11_features,
      .drawIndirectCount = true,
      .storageBuffer8BitAccess = true,
      .shaderInt8 = true,
      .descriptorBindingSampledImageUpdateAfterBind = true,
      .descriptorBindingStorageImageUpdateAfterBind = true,
      .descriptorBindingPartiallyBound = true,
      .scalarBlockLayout = true,
      .timelineSemaphore = true,
      .bufferDeviceAddress = true,
  };

  VkPhysicalDeviceVulkan13Features vulkan13_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &vulkan12_features,
      .synchronization2 = true,
      .dynamicRendering = true,
      .maintenance4 = true,
  };

  std::array extensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };

  VkDeviceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &vulkan13_features,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };

  VkDevice device;
  throw_if_failed(vkCreateDevice(adapter, &create_info, nullptr, &device),
                  "Vulkan: Failed to create device");

  return UniqueDevice(device);
}

auto create_allocator(VkInstance instance, VkPhysicalDevice adapter,
                      VkDevice device) -> UniqueAllocator {
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
      .vulkanApiVersion = Renderer::getRequiredAPIVersion(),
  };

  VmaAllocator allocator;
  throw_if_failed(vmaCreateAllocator(&allocator_info, &allocator),
                  "VMA: Failed to create allocator");

  return UniqueAllocator(allocator);
}

} // namespace

Renderer::Renderer(Span<const char *const> extensions, u32 adapter) {
  throw_if_failed(volkInitialize(), "Volk: failed to initialize");

  m_instance = create_instance(extensions);

  volkLoadInstanceOnly(get_instance());

  m_adapter = find_adapter(get_instance(), adapter);
  if (!m_adapter) {
    throw std::runtime_error("Vulkan: Failed to find requested adapter");
  }

  Optional<usize> graphics_queue_family = find_graphics_queue_family(m_adapter);
  if (graphics_queue_family) {
    m_graphics_queue_family = *graphics_queue_family;
  } else {
    throw std::runtime_error("Vulkan: Failed to find graphics queue");
  }

  m_device = create_device(m_adapter, m_graphics_queue_family);

  volkLoadDevice(get_device());

  vkGetDeviceQueue(get_device(), m_graphics_queue_family, 0, &m_graphics_queue);
  m_graphics_queue_semaphore = create_semaphore({
      .name = "Device time semaphore",
      .initial_value = 0,
  });

  m_allocator = create_allocator(get_instance(), m_adapter, get_device());
} // namespace ren

#define define_device_deleter(T, F)                                            \
  template <> struct QueueDeleter<T> {                                         \
    void operator()(Renderer &renderer, T handle) const noexcept {             \
      F(renderer.get_device(), handle, nullptr);                               \
    }                                                                          \
  }

define_device_deleter(VkBuffer, vkDestroyBuffer);
define_device_deleter(VkDescriptorPool, vkDestroyDescriptorPool);
define_device_deleter(VkDescriptorSetLayout, vkDestroyDescriptorSetLayout);
define_device_deleter(VkImage, vkDestroyImage);
define_device_deleter(VkImageView, vkDestroyImageView);
define_device_deleter(VkPipeline, vkDestroyPipeline);
define_device_deleter(VkPipelineLayout, vkDestroyPipelineLayout);
define_device_deleter(VkSampler, vkDestroySampler);
define_device_deleter(VkSemaphore, vkDestroySemaphore);
define_device_deleter(VkSwapchainKHR, vkDestroySwapchainKHR);

#undef define_device_deleter

template <> struct QueueDeleter<VkSurfaceKHR> {
  void operator()(Renderer &renderer, VkSurfaceKHR handle) const noexcept {
    vkDestroySurfaceKHR(renderer.get_instance(), handle, nullptr);
  }
};

template <> struct QueueDeleter<VmaAllocation> {
  void operator()(Renderer &renderer, VmaAllocation allocation) const noexcept {
    vmaFreeMemory(renderer.get_allocator(), allocation);
  }
};

Renderer::~Renderer() {
  destroy(m_graphics_queue_semaphore);
  flush();
}

auto Renderer::create_scene(ISwapchain &swapchain)
    -> expected<std::unique_ptr<IScene>> {
  return std::make_unique<Scene>(*this, static_cast<Swapchain &>(swapchain));
}

void Renderer::flush() {
  throw_if_failed(vkDeviceWaitIdle(get_device()),
                  "Vulkan: Failed to wait for idle device");
  m_delete_queue.flush(*this);
}

void Renderer::next_frame() {
  m_frame_end_times[m_frame_index] = m_graphics_queue_time;
  m_frame_index = (m_frame_index + 1) % m_frame_end_times.size();
  wait_for_semaphore(get_semaphore(m_graphics_queue_semaphore),
                     m_frame_end_times[m_frame_index]);
  m_delete_queue.next_frame(*this);
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
  m_descriptor_pools.try_pop(pool).map(
      [&](const DescriptorPool &pool) { push_to_delete_queue(pool.handle); });
}

auto Renderer::try_get_descriptor_pool(Handle<DescriptorPool> pool) const
    -> Optional<const DescriptorPool &> {
  return m_descriptor_pools.get(pool);
}

auto Renderer::get_descriptor_pool(Handle<DescriptorPool> pool) const
    -> const DescriptorPool & {
  assert(m_descriptor_pools.contains(pool));
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
  auto binding_flags =
      create_info.bindings |
      filter_map([](const DescriptorBinding &binding)
                     -> Optional<VkDescriptorBindingFlags> {
        if (binding.count == 0) {
          return None;
        }
        return binding.flags;
      }) |
      ranges::to<
          StaticVector<VkDescriptorBindingFlags, MAX_DESCIPTOR_BINDINGS>>;

  auto bindings =
      enumerate(create_info.bindings) |
      filter_map([](const auto &p) -> Optional<VkDescriptorSetLayoutBinding> {
        const auto &[index, binding] = p;
        if (binding.count == 0) {
          return None;
        }
        return VkDescriptorSetLayoutBinding{
            .binding = unsigned(index),
            .descriptorType = binding.type,
            .descriptorCount = binding.count,
            .stageFlags = binding.stages,
        };
      }) |
      ranges::to<
          StaticVector<VkDescriptorSetLayoutBinding, MAX_DESCIPTOR_BINDINGS>>;

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
        push_to_delete_queue(layout.handle);
      });
}

auto Renderer::try_get_descriptor_set_layout(Handle<DescriptorSetLayout> layout)
    const -> Optional<const DescriptorSetLayout &> {
  return m_descriptor_set_layouts.get(layout);
}

auto Renderer::get_descriptor_set_layout(
    Handle<DescriptorSetLayout> layout) const -> const DescriptorSetLayout & {
  assert(m_descriptor_set_layouts.contains(layout));
  return m_descriptor_set_layouts[layout];
}

auto Renderer::allocate_descriptor_sets(
    Handle<DescriptorPool> pool,
    TempSpan<const Handle<DescriptorSetLayout>> layouts,
    VkDescriptorSet *sets) const -> bool {
  auto vk_layouts = layouts | map([&](Handle<DescriptorSetLayout> layout) {
                      return get_descriptor_set_layout(layout).handle;
                    }) |
                    ranges::to<SmallVector<VkDescriptorSetLayout>>;

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
  assert(create_info.size > 0);

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
    push_to_delete_queue(buffer.handle);
    push_to_delete_queue(buffer.allocation);
  });
}

auto Renderer::try_get_buffer(Handle<Buffer> buffer) const
    -> Optional<const Buffer &> {
  return m_buffers.get(buffer);
};

auto Renderer::get_buffer(Handle<Buffer> buffer) const -> const Buffer & {
  assert(m_buffers.contains(buffer));
  return m_buffers[buffer];
};

auto Renderer::try_get_buffer_view(Handle<Buffer> handle) const
    -> Optional<BufferView> {
  return try_get_buffer(handle).map([&](const Buffer &buffer) -> BufferView {
    return {
        .buffer = handle,
        .size = buffer.size,
    };
  });
};

auto Renderer::get_buffer_view(Handle<Buffer> handle) const -> BufferView {
  const auto &buffer = get_buffer(handle);
  return {
      .buffer = handle,
      .size = buffer.size,
  };
};

auto Renderer::create_texture(const TextureCreateInfo &&create_info)
    -> Handle<Texture> {
  assert(create_info.width > 0);
  assert(create_info.height > 0);
  assert(create_info.depth > 0);
  assert(create_info.num_mip_levels > 0);
  assert(create_info.num_array_layers > 0);

  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = create_info.type,
      .format = create_info.format,
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
      push_to_delete_queue(texture.image);
      push_to_delete_queue(texture.allocation);
    }
    for (const auto &[_, view] : m_image_views[handle]) {
      push_to_delete_queue(view);
    }
    m_image_views.erase(handle);
  });
}

auto Renderer::try_get_texture(Handle<Texture> texture) const
    -> Optional<const Texture &> {
  return m_textures.get(texture);
}

auto Renderer::get_texture(Handle<Texture> texture) const -> const Texture & {
  assert(m_textures.contains(texture));
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
  assert(view.first_mip_level + mip_level_offset < texture.num_mip_levels);
  return get_size_at_mip_level(texture.size,
                               view.first_mip_level + mip_level_offset);
}

auto Renderer::getVkImageView(const TextureView &view) -> VkImageView {
  auto [it, inserted] = m_image_views[view.texture].insert(view, nullptr);
  auto &image_view = std::get<1>(*it);
  if (inserted) {
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = get_texture(view.texture).image,
        .viewType = view.type,
        .format = view.format,
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
    throw_if_failed(
        vkCreateImageView(get_device(), &view_info, nullptr, &image_view),
        "Vulkan: Failed to create image view");
  }
  return image_view;
}

auto Renderer::create_sampler(const SamplerCreateInfo &&create_info)
    -> Handle<Sampler> {
  VkSamplerCreateInfo sampler_info = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
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
  m_samplers.try_pop(sampler).map(
      [&](const Sampler &sampler) { push_to_delete_queue(sampler.handle); });
}

auto Renderer::get_sampler(Handle<Sampler> sampler) const -> const Sampler & {
  assert(m_samplers.contains(sampler));
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
  m_semaphores.try_pop(semaphore).map(
      [&](Semaphore semaphore) { push_to_delete_queue(semaphore.handle); });
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
  assert(result == VK_SUCCESS);
}

auto Renderer::try_get_semaphore(Handle<Semaphore> semaphore) const
    -> Optional<const Semaphore &> {
  return m_semaphores.get(semaphore);
}

auto Renderer::get_semaphore(Handle<Semaphore> semaphore) const
    -> const Semaphore & {
  assert(m_semaphores.contains(semaphore));
  return m_semaphores[semaphore];
}

void Renderer::queueSubmit(
    VkQueue queue, TempSpan<const VkCommandBufferSubmitInfo> cmd_buffers,
    TempSpan<const VkSemaphoreSubmitInfo> wait_semaphores,
    TempSpan<const VkSemaphoreSubmitInfo> input_signal_semaphores) {
  SmallVector<VkSemaphoreSubmitInfo, 8> signal_semaphores(
      input_signal_semaphores);
  signal_semaphores.push_back({
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = get_semaphore(m_graphics_queue_semaphore).handle,
      .value = ++m_graphics_queue_time,
  });

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
  assert(code.size() % sizeof(u32) == 0);
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
        return attachment.format;
      }) |
      ranges::to<StaticVector<VkFormat, MAX_COLOR_ATTACHMENTS>>;

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
      .cullMode = create_info.rasterization.cull_mode,
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
    rendering_info.depthAttachmentFormat = depth_test.format;
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
      ranges::to<StaticVector<VkPipelineColorBlendAttachmentState,
                              MAX_COLOR_ATTACHMENTS>>;

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
        push_to_delete_queue(pipeline.handle);
      });
}

auto Renderer::try_get_graphics_pipeline(Handle<GraphicsPipeline> pipeline)
    const -> Optional<const GraphicsPipeline &> {
  return m_graphics_pipelines.get(pipeline);
}

auto Renderer::get_graphics_pipeline(Handle<GraphicsPipeline> pipeline) const
    -> const GraphicsPipeline & {
  assert(m_graphics_pipelines.contains(pipeline));
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
        push_to_delete_queue(pipeline.handle);
      });
}

auto Renderer::try_get_compute_pipeline(Handle<ComputePipeline> pipeline) const
    -> Optional<const ComputePipeline &> {
  return m_compute_pipelines.get(pipeline);
}

auto Renderer::get_compute_pipeline(Handle<ComputePipeline> pipeline) const
    -> const ComputePipeline & {
  assert(m_compute_pipelines.contains(pipeline));
  return m_compute_pipelines[pipeline];
}

auto Renderer::create_pipeline_layout(
    const PipelineLayoutCreateInfo &&create_info) -> Handle<PipelineLayout> {
  auto layouts =
      create_info.set_layouts | map([&](Handle<DescriptorSetLayout> layout) {
        return get_descriptor_set_layout(layout).handle;
      }) |
      ranges::to<StaticVector<VkDescriptorSetLayout, MAX_DESCRIPTOR_SETS>>;

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
    push_to_delete_queue(layout.handle);
  });
}

auto Renderer::try_get_pipeline_layout(Handle<PipelineLayout> layout) const
    -> Optional<const PipelineLayout &> {
  return m_pipeline_layouts.get(layout);
}

auto Renderer::get_pipeline_layout(Handle<PipelineLayout> layout) const
    -> const PipelineLayout & {
  assert(m_pipeline_layouts.contains(layout));
  return m_pipeline_layouts[layout];
}

auto Renderer::queue_present(const VkPresentInfoKHR &present_info) -> VkResult {
  VkQueue queue = getGraphicsQueue();
  VkResult result = vkQueuePresentKHR(queue, &present_info);
  switch (result) {
  default:
    break;
  case VK_SUCCESS:
  case VK_SUBOPTIMAL_KHR:
  case VK_ERROR_OUT_OF_DATE_KHR:
  case VK_ERROR_SURFACE_LOST_KHR:
  case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: {
#if 1
    queueSubmit(queue, {});
#else
    // NOTE: bad stuff (like a dead lock) will happen if someones tries to
    // wait for this value before signaling the graphics queue with a higher
    // value.
    ++m_graphics_queue_time;
#endif
  }
  }
  return result;
}

} // namespace ren
