#include "Device.hpp"
#include "Errors.hpp"
#include "Formats.inl"
#include "Support/Array.hpp"
#include "Support/Views.hpp"
#include "Swapchain.hpp"

namespace ren {

#if REN_DEBUG_NAMES

#define ren_set_debug_name(object, name)                                       \
  {                                                                            \
    VkDebugUtilsObjectNameInfoEXT name_info = {                                \
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,           \
        .objectType = [&]() consteval {using T = decltype(object);             \
    if constexpr (std::same_as<T, VkBuffer>) {                                 \
      return VK_OBJECT_TYPE_BUFFER;                                            \
    } else if constexpr (std::same_as<T, VkImage>) {                           \
      return VK_OBJECT_TYPE_IMAGE;                                             \
    } else if constexpr (std::same_as<T, VkSampler>) {                         \
      return VK_OBJECT_TYPE_SAMPLER;                                           \
    } else if constexpr (std::same_as<T, VkSemaphore>) {                       \
      return VK_OBJECT_TYPE_SEMAPHORE;                                         \
    } else if constexpr (std::same_as<T, VkDescriptorPool>) {                  \
      return VK_OBJECT_TYPE_DESCRIPTOR_POOL;                                   \
    } else if constexpr (std::same_as<T, VkDescriptorSetLayout>) {             \
      return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;                             \
    } else if constexpr (std::same_as<T, VkPipelineLayout>) {                  \
      return VK_OBJECT_TYPE_PIPELINE_LAYOUT;                                   \
    } else if constexpr (std::same_as<T, VkPipeline>) {                        \
      return VK_OBJECT_TYPE_PIPELINE;                                          \
    }                                                                          \
    throw("Unknown debug object type");                                        \
  }                                                                            \
  (), .objectHandle = (uint64_t)object, .pObjectName = name,                   \
  }                                                                            \
  ;                                                                            \
  throwIfFailed(SetDebugUtilsObjectNameEXT(&name_info),                        \
                "Vulkan: Failed to set object name");                          \
  }

#else

#define ren_set_debug_name(object, name)

#endif

std::span<const char *const> Device::getRequiredLayers() noexcept {
  static constexpr auto layers = makeArray<const char *>(
#if REN_VULKAN_VALIDATION
      "VK_LAYER_KHRONOS_validation"
#endif
  );
  return layers;
}

std::span<const char *const> Device::getInstanceExtensions() noexcept {
  static constexpr auto extensions = makeArray<const char *>(
#if REN_DEBUG_NAMES
      VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
  );
  return extensions;
}

namespace {
int findQueueFamilyWithCapabilities(Device *device, VkQueueFlags caps) {
  unsigned qcnt = 0;
  device->GetPhysicalDeviceQueueFamilyProperties(&qcnt, nullptr);
  SmallVector<VkQueueFamilyProperties, 4> queues(qcnt);
  device->GetPhysicalDeviceQueueFamilyProperties(&qcnt, queues.data());
  for (unsigned i = 0; i < qcnt; ++i) {
    constexpr auto filter =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    if ((queues[i].queueFlags & filter) == caps) {
      return i;
    }
  }
  return -1;
}

int findGraphicsQueueFamily(Device *device) {
  return findQueueFamilyWithCapabilities(device, VK_QUEUE_GRAPHICS_BIT |
                                                     VK_QUEUE_COMPUTE_BIT |
                                                     VK_QUEUE_TRANSFER_BIT);
}
} // namespace

Device::Device(PFN_vkGetInstanceProcAddr proc, VkInstance instance,
               VkPhysicalDevice m_adapter)
    : m_instance(instance), m_adapter(m_adapter) {
  loadInstanceFunctions(proc, m_instance, &m_vk);

  m_graphics_queue_family = findGraphicsQueueFamily(this);

  float queue_priority = 1.0f;
  VkDeviceQueueCreateInfo queue_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = m_graphics_queue_family,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
  };

  VkPhysicalDeviceFeatures2 vulkan10_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .features = {
          .shaderInt64 = true,
      }};

  VkPhysicalDeviceVulkan11Features vulkan11_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .pNext = &vulkan10_features,
  };

  VkPhysicalDeviceVulkan12Features vulkan12_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &vulkan11_features,
      .descriptorBindingSampledImageUpdateAfterBind = true,
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
  };

  std::array extensions = {
      VK_GOOGLE_HLSL_FUNCTIONALITY1_EXTENSION_NAME,
      VK_GOOGLE_USER_TYPE_EXTENSION_NAME,
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

  throwIfFailed(CreateDevice(&create_info, &m_device),
                "Vulkan: Failed to create device");

  loadDeviceFunctions(m_vk.GetDeviceProcAddr, m_device, &m_vk);

  GetDeviceQueue(m_graphics_queue_family, 0, &m_graphics_queue);
  m_graphics_queue_semaphore = create_semaphore({
      REN_SET_DEBUG_NAME("Device time semaphore"),
      .initial_value = 0,
  });

  VmaVulkanFunctions vma_vulkan_functions = {
      .vkGetInstanceProcAddr = m_vk.GetInstanceProcAddr,
      .vkGetDeviceProcAddr = m_vk.GetDeviceProcAddr,
  };

  VmaAllocatorCreateInfo allocator_info = {
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = m_adapter,
      .device = m_device,
      .pAllocationCallbacks = getAllocator(),
      .pVulkanFunctions = &vma_vulkan_functions,
      .instance = m_instance,
      .vulkanApiVersion = getRequiredAPIVersion(),
  };

  throwIfFailed(vmaCreateAllocator(&allocator_info, &m_allocator),
                "VMA: Failed to create allocator");
}

#define define_queue_deleter(T, F)                                             \
  template <> struct QueueDeleter<T> {                                         \
    void operator()(Device &device, T handle) const noexcept {                 \
      device.F(handle);                                                        \
    }                                                                          \
  }

define_queue_deleter(VkBuffer, DestroyBuffer);
define_queue_deleter(VkDescriptorPool, DestroyDescriptorPool);
define_queue_deleter(VkDescriptorSetLayout, DestroyDescriptorSetLayout);
define_queue_deleter(VkImage, DestroyImage);
define_queue_deleter(VkImageView, DestroyImageView);
define_queue_deleter(VkPipeline, DestroyPipeline);
define_queue_deleter(VkPipelineLayout, DestroyPipelineLayout);
define_queue_deleter(VkSampler, DestroySampler);
define_queue_deleter(VkSemaphore, DestroySemaphore);
define_queue_deleter(VkSurfaceKHR, DestroySurfaceKHR);
define_queue_deleter(VkSwapchainKHR, DestroySwapchainKHR);

#undef define_queue_deleter

template <> struct QueueDeleter<VmaAllocation> {
  void operator()(Device &device, VmaAllocation allocation) const noexcept {
    vmaFreeMemory(device.getVMAAllocator(), allocation);
  }
};

Device::~Device() {
  destroy_semaphore(m_graphics_queue_semaphore);
  flush();
  vmaDestroyAllocator(m_allocator);
  DestroyDevice();
  DestroyInstance();
}

void Device::flush() {
  DeviceWaitIdle();
  m_delete_queue.flush(*this);
}

void Device::next_frame() {
  m_frame_end_times[m_frame_index] = m_graphics_queue_time;
  m_frame_index = (m_frame_index + 1) % m_frame_end_times.size();
  wait_for_semaphore(get_semaphore(m_graphics_queue_semaphore),
                     m_frame_end_times[m_frame_index]);
  m_delete_queue.next_frame(*this);
}

auto Device::create_descriptor_pool(
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
  throwIfFailed(CreateDescriptorPool(&pool_info, &pool),
                "Vulkan: Failed to create descriptor pool");
  ren_set_debug_name(pool, create_info.debug_name.c_str());

  return m_descriptor_pools.emplace(DescriptorPool{
      .handle = pool,
      .flags = create_info.flags,
      .set_count = create_info.set_count,
      .pool_sizes = create_info.pool_sizes,
  });
}

void Device::destroy_descriptor_pool(Handle<DescriptorPool> pool) {
  m_descriptor_pools.try_pop(pool).map(
      [&](const DescriptorPool &pool) { push_to_delete_queue(pool.handle); });
}

auto Device::try_get_descriptor_pool(Handle<DescriptorPool> pool) const
    -> Optional<const DescriptorPool &> {
  return m_descriptor_pools.get(pool);
}

auto Device::get_descriptor_pool(Handle<DescriptorPool> pool) const
    -> const DescriptorPool & {
  assert(m_descriptor_pools.contains(pool));
  return m_descriptor_pools[pool];
}

void Device::reset_descriptor_pool(Handle<DescriptorPool> pool) const {
  ResetDescriptorPool(get_descriptor_pool(pool).handle, 0);
}

auto Device::create_descriptor_set_layout(
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
  throwIfFailed(CreateDescriptorSetLayout(&layout_info, &layout),
                "Vulkann: Failed to create descriptor set layout");
  ren_set_debug_name(layout, create_info.debug_name.c_str());

  return m_descriptor_set_layouts.emplace(DescriptorSetLayout{
      .handle = layout,
      .flags = create_info.flags,
      .bindings = create_info.bindings,
  });
}

void Device::destroy_descriptor_set_layout(Handle<DescriptorSetLayout> layout) {
  m_descriptor_set_layouts.try_pop(layout).map(
      [&](const DescriptorSetLayout &layout) {
        push_to_delete_queue(layout.handle);
      });
}

auto Device::try_get_descriptor_set_layout(Handle<DescriptorSetLayout> layout)
    const -> Optional<const DescriptorSetLayout &> {
  return m_descriptor_set_layouts.get(layout);
}

auto Device::get_descriptor_set_layout(Handle<DescriptorSetLayout> layout) const
    -> const DescriptorSetLayout & {
  assert(m_descriptor_set_layouts.contains(layout));
  return m_descriptor_set_layouts[layout];
}

auto Device::allocate_descriptor_sets(
    Handle<DescriptorPool> pool,
    std::span<const Handle<DescriptorSetLayout>> layouts,
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

  auto result = AllocateDescriptorSets(&alloc_info, sets);
  switch (result) {
  default: {
    throwIfFailed(result, "Vulkan: Failed to allocate descriptor sets");
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

auto Device::allocate_descriptor_set(Handle<DescriptorPool> pool,
                                     Handle<DescriptorSetLayout> layout) const
    -> Optional<VkDescriptorSet> {
  VkDescriptorSet set;
  if (allocate_descriptor_sets(pool, {&layout, 1}, &set)) {
    return set;
  }
  return None;
}

void Device::write_descriptor_sets(
    std::span<const VkWriteDescriptorSet> configs) const {
  UpdateDescriptorSets(configs.size(), configs.data(), 0, nullptr);
}

void Device::write_descriptor_set(const VkWriteDescriptorSet &config) const {
  write_descriptor_sets({&config, 1});
}

auto Device::create_buffer(const BufferCreateInfo &&create_info)
    -> Handle<Buffer> {
  assert(create_info.size > 0);

  VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = create_info.size,
      .usage = create_info.usage,
  };

  VmaAllocationCreateInfo alloc_info = {
      .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO,
  };

  switch (create_info.heap) {
    using enum BufferHeap;
  case Device:
    alloc_info.flags |=
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    break;
  case Upload:
    alloc_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    break;
  case Readback:
    alloc_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    break;
  }

  VkBuffer buffer;
  VmaAllocation allocation;
  VmaAllocationInfo map_info;
  throwIfFailed(vmaCreateBuffer(m_allocator, &buffer_info, &alloc_info, &buffer,
                                &allocation, &map_info),
                "VMA: Failed to create buffer");
  ren_set_debug_name(buffer, create_info.debug_name.c_str());

  uint64_t address = 0;
  if (create_info.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
    VkBufferDeviceAddressInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer,
    };
    address = GetBufferDeviceAddress(&buffer_info);
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

void Device::destroy_buffer(Handle<Buffer> handle) {
  m_buffers.try_pop(handle).map([&](const Buffer &buffer) {
    push_to_delete_queue(buffer.handle);
    push_to_delete_queue(buffer.allocation);
  });
}

auto Device::try_get_buffer(Handle<Buffer> buffer) const
    -> Optional<const Buffer &> {
  return m_buffers.get(buffer);
};

auto Device::get_buffer(Handle<Buffer> buffer) const -> const Buffer & {
  return m_buffers[buffer];
};

auto Device::create_texture(const TextureCreateInfo &&create_info)
    -> Handle<Texture> {
  unsigned depth = 1;
  unsigned array_layers = 1;
  if (create_info.type == VK_IMAGE_TYPE_3D) {
    depth = create_info.depth;
  } else {
    array_layers = create_info.array_layers;
  }

  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = create_info.type,
      .format = create_info.format,
      .extent = {create_info.width, create_info.height, depth},
      .mipLevels = create_info.mip_levels,
      .arrayLayers = array_layers,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = create_info.usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO};

  VkImage image;
  VmaAllocation allocation;
  throwIfFailed(vmaCreateImage(m_allocator, &image_info, &alloc_info, &image,
                               &allocation, nullptr),
                "VMA: Failed to create image");
  ren_set_debug_name(image, create_info.debug_name.c_str());

  return m_textures.emplace(Texture{
      .image = image,
      .allocation = allocation,
      .type = create_info.type,
      .format = create_info.format,
      .usage = create_info.usage,
      .size = {create_info.width, create_info.height, depth},
      .num_mip_levels = create_info.mip_levels,
      .num_array_layers = create_info.array_layers,
  });
}

auto Device::create_swapchain_texture(
    const SwapchainTextureCreateInfo &&create_info) -> Handle<Texture> {
  ren_set_debug_name(create_info.image, "Swapchain image");

  return m_textures.emplace(Texture{
      .image = create_info.image,
      .type = VK_IMAGE_TYPE_2D,
      .format = create_info.format,
      .usage = create_info.usage,
      .size = {create_info.width, create_info.height, 1u},
      .num_mip_levels = 1,
      .num_array_layers = 1,
  });
}

void Device::destroy_texture(Handle<Texture> handle) {
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

auto Device::try_get_texture(Handle<Texture> texture) const
    -> Optional<const Texture &> {
  return m_textures.get(texture);
}

auto Device::get_texture(Handle<Texture> texture) const -> const Texture & {
  assert(m_textures.contains(texture));
  return m_textures[texture];
}

auto Device::getVkImageView(const TextureView &view) -> VkImageView {
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
    throwIfFailed(CreateImageView(&view_info, &image_view),
                  "Vulkan: Failed to create image view");
  }
  return image_view;
}

auto Device::create_sampler(const SamplerCreateInfo &&create_info)
    -> Handle<Sampler> {
  VkSamplerCreateInfo sampler_info = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = create_info.mag_filter,
      .minFilter = create_info.min_filter,
      .mipmapMode = create_info.mipmap_mode,
      .addressModeU = create_info.address_mode_u,
      .addressModeV = create_info.address_mode_v,
      .maxLod = VK_LOD_CLAMP_NONE,
  };

  VkSampler sampler;
  throwIfFailed(CreateSampler(&sampler_info, &sampler),
                "Vulkan: Failed to create sampler");
  ren_set_debug_name(sampler, create_info.debug_name.c_str());

  return m_samplers.emplace(Sampler{
      .handle = sampler,
      .mag_filter = create_info.mag_filter,
      .min_filter = create_info.min_filter,
      .mipmap_mode = create_info.mipmap_mode,
      .address_mode_u = create_info.address_mode_u,
      .address_mode_v = create_info.address_mode_v,
  });
}

void Device::destroy_sampler(Handle<Sampler> sampler) {
  m_samplers.try_pop(sampler).map(
      [&](const Sampler &sampler) { push_to_delete_queue(sampler.handle); });
}

auto Device::get_sampler(Handle<Sampler> sampler) const -> const Sampler & {
  assert(m_samplers.contains(sampler));
  return m_samplers[sampler];
}

auto Device::create_semaphore(const SemaphoreCreateInfo &&create_info)
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
  throwIfFailed(CreateSemaphore(&semaphore_info, &semaphore),
                "Vulkan: Failed to create semaphore");
  ren_set_debug_name(semaphore, create_info.debug_name.c_str());

  return m_semaphores.emplace(Semaphore{.handle = semaphore});
}

void Device::destroy_semaphore(Handle<Semaphore> semaphore) {
  m_semaphores.try_pop(semaphore).map(
      [&](Semaphore semaphore) { push_to_delete_queue(semaphore.handle); });
}

auto Device::wait_for_semaphore(const Semaphore &semaphore, uint64_t value,
                                std::chrono::nanoseconds timeout) const
    -> VkResult {
  VkSemaphoreWaitInfo wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &semaphore.handle,
      .pValues = &value,
  };
  auto result = WaitSemaphores(&wait_info, timeout.count());
  switch (result) {
  case VK_SUCCESS:
  case VK_TIMEOUT:
    return result;
  default:
    throw std::runtime_error{"Vulkan: Failed to wait for semaphore"};
  };
}

void Device::wait_for_semaphore(const Semaphore &semaphore,
                                uint64_t value) const {
  auto result = wait_for_semaphore(semaphore, value,
                                   std::chrono::nanoseconds(UINT64_MAX));
  assert(result == VK_SUCCESS);
}

auto Device::try_get_semaphore(Handle<Semaphore> semaphore) const
    -> Optional<const Semaphore &> {
  return m_semaphores.get(semaphore);
}

auto Device::get_semaphore(Handle<Semaphore> semaphore) const
    -> const Semaphore & {
  assert(m_semaphores.contains(semaphore));
  return m_semaphores[semaphore];
}

void Device::queueSubmit(
    VkQueue queue, std::span<const VkCommandBufferSubmitInfo> cmd_buffers,
    std::span<const VkSemaphoreSubmitInfo> wait_semaphores,
    std::span<const VkSemaphoreSubmitInfo> input_signal_semaphores) {
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

  throwIfFailed(QueueSubmit2(queue, 1, &submit_info, VK_NULL_HANDLE),
                "Vulkan: Failed to submit work to queue");
}

auto Device::create_graphics_pipeline(
    const GraphicsPipelineCreateInfo &&create_info)
    -> Handle<GraphicsPipeline> {
  constexpr size_t MAX_GRAPHICS_SHADER_STAGES = 2;

  StaticVector<VkPipelineShaderStageCreateInfo, MAX_GRAPHICS_SHADER_STAGES>
      shaders;
  StaticVector<VkShaderModule, MAX_GRAPHICS_SHADER_STAGES> shader_modules;

  auto add_shader = [&](VkShaderStageFlagBits stage, const ShaderInfo &shader) {
    assert(shader.code.size() % sizeof(u32) == 0);
    VkShaderModuleCreateInfo module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader.code.size(),
        .pCode = reinterpret_cast<const u32 *>(shader.code.data()),
    };
    VkShaderModule module;
    throwIfFailed(CreateShaderModule(&module_info, &module),
                  "Vulkan: Failed to create shader module");
    shaders.push_back(VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage,
        .module = module,
        .pName = shader.entry_point,
    });
    shader_modules.push_back(module);
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
    depth_stencil_info.depthCompareOp = depth_test.compare_op;
  });

  auto color_attachments =
      create_info.color_attachments |
      map([&](const ColorAttachmentInfo &attachment) {
        return VkPipelineColorBlendAttachmentState{
            .colorWriteMask = attachment.write_mask,
        };
      }) |
      ranges::to<StaticVector<VkPipelineColorBlendAttachmentState,
                              MAX_COLOR_ATTACHMENTS>>;

  VkPipelineColorBlendStateCreateInfo blend_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = u32(color_attachments.size()),
      .pAttachments = color_attachments.data(),
  };

  std::array dynamic_states = {
      VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
      VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
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
  throwIfFailed(CreateGraphicsPipelines(nullptr, 1, &pipeline_info, &pipeline),
                "Vulkan: Failed to create graphics pipeline");
  ren_set_debug_name(pipeline, create_info.debug_name.c_str());
  for (auto module : shader_modules) {
    DestroyShaderModule(module);
  }

  return m_graphics_pipelines.emplace(GraphicsPipeline{
      .handle = pipeline,
      .layout = create_info.layout,
      .input_assembly = create_info.input_assembly,
      .multisample = create_info.multisample,
      .depth_test = create_info.depth_test,
      .color_attachments = create_info.color_attachments,
  });
}

void Device::destroy_graphics_pipeline(Handle<GraphicsPipeline> pipeline) {
  m_graphics_pipelines.try_pop(pipeline).map(
      [&](const GraphicsPipeline &pipeline) {
        push_to_delete_queue(pipeline.handle);
      });
}

auto Device::try_get_graphics_pipeline(Handle<GraphicsPipeline> pipeline) const
    -> Optional<const GraphicsPipeline &> {
  return m_graphics_pipelines.get(pipeline);
}

auto Device::get_graphics_pipeline(Handle<GraphicsPipeline> pipeline) const
    -> const GraphicsPipeline & {
  assert(m_graphics_pipelines.contains(pipeline));
  return m_graphics_pipelines[pipeline];
}

auto Device::create_pipeline_layout(
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
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &create_info.push_constants,
  };

  VkPipelineLayout layout;
  throwIfFailed(CreatePipelineLayout(&layout_info, &layout),
                "Vulkan: Failed to create pipeline layout");
  ren_set_debug_name(layout, create_info.debug_name.c_str());

  return m_pipeline_layouts.emplace(PipelineLayout{
      .handle = layout,
      .set_layouts{create_info.set_layouts},
      .push_constants = create_info.push_constants,
  });
}

void Device::destroy_pipeline_layout(Handle<PipelineLayout> layout) {
  m_pipeline_layouts.try_pop(layout).map([&](const PipelineLayout &layout) {
    push_to_delete_queue(layout.handle);
  });
}

auto Device::try_get_pipeline_layout(Handle<PipelineLayout> layout) const
    -> Optional<const PipelineLayout &> {
  return m_pipeline_layouts.get(layout);
}

auto Device::get_pipeline_layout(Handle<PipelineLayout> layout) const
    -> const PipelineLayout & {
  assert(m_pipeline_layouts.contains(layout));
  return m_pipeline_layouts[layout];
}

auto Device::queuePresent(const VkPresentInfoKHR &present_info) -> VkResult {
  auto queue = getGraphicsQueue();
  auto r = QueuePresentKHR(queue, &present_info);
  switch (r) {
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
  return r;
}

} // namespace ren
