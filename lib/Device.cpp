#include "Device.hpp"
#include "Errors.hpp"
#include "Formats.inl"
#include "Support/Array.hpp"
#include "Support/Views.hpp"

namespace ren {

std::span<const char *const> Device::getRequiredLayers() noexcept {
  static constexpr auto layers = makeArray<const char *>(
#if REN_VULKAN_VALIDATION
      "VK_LAYER_KHRONOS_validation"
#endif
  );
  return layers;
}

std::span<const char *const> Device::getRequiredExtensions() noexcept {
  static constexpr auto extensions = makeArray<const char *>();
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
  m_graphics_queue_semaphore = createTimelineSemaphore();

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

template <> struct QueueDeleter<ImageViews> {
  void operator()(Device &device, ImageViews views) const noexcept {
    device.destroy_image_views(views.image);
  }
};

template <> struct QueueDeleter<VmaAllocation> {
  void operator()(Device &device, VmaAllocation allocation) const noexcept {
    vmaFreeMemory(device.getVMAAllocator(), allocation);
  }
};

define_queue_deleter(VkBuffer, DestroyBuffer);
define_queue_deleter(VkDescriptorPool, DestroyDescriptorPool);
define_queue_deleter(VkDescriptorSetLayout, DestroyDescriptorSetLayout);
define_queue_deleter(VkImage, DestroyImage);
define_queue_deleter(VkPipeline, DestroyPipeline);
define_queue_deleter(VkPipelineLayout, DestroyPipelineLayout);
define_queue_deleter(VkSemaphore, DestroySemaphore);
define_queue_deleter(VkSurfaceKHR, DestroySurfaceKHR);
define_queue_deleter(VkSwapchainKHR, DestroySwapchainKHR);

#undef define_queue_deleter

Device::~Device() {
  flush();
  DestroySemaphore(m_graphics_queue_semaphore);
  vmaDestroyAllocator(m_allocator);
  DestroyDevice();
}

void Device::flush() {
  DeviceWaitIdle();
  m_delete_queue.flush(*this);
}

void Device::next_frame() {
  m_frame_end_times[m_frame_index].graphics_queue_time = getGraphicsQueueTime();
  m_frame_index = (m_frame_index + 1) % m_frame_end_times.size();
  waitForGraphicsQueue(m_frame_end_times[m_frame_index].graphics_queue_time);
  m_delete_queue.next_frame(*this);
}

auto Device::create_descriptor_pool(const DescriptorPoolDesc &desc)
    -> DescriptorPool {
  StaticVector<VkDescriptorPoolSize,
               std::tuple_size_v<decltype(DescriptorPoolDesc::pool_sizes)>>
      pool_sizes;

  for (int i = 0; i < DESCRIPTOR_TYPE_COUNT; ++i) {
    auto type = static_cast<VkDescriptorType>(i);
    auto count = desc.pool_sizes[type];
    if (count > 0) {
      pool_sizes.push_back({
          .type = type,
          .descriptorCount = count,
      });
    }
  }

  VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = desc.flags,
      .maxSets = desc.set_count,
      .poolSizeCount = unsigned(pool_sizes.size()),
      .pPoolSizes = pool_sizes.data(),
  };

  VkDescriptorPool pool;
  throwIfFailed(CreateDescriptorPool(&pool_info, &pool),
                "Vulkan: Failed to create descriptor pool");

  return {.desc = desc, .handle = {pool, [this](VkDescriptorPool pool) {
                                     push_to_delete_queue(pool);
                                   }}};
}

void Device::reset_descriptor_pool(const DescriptorPoolRef &pool) {
  ResetDescriptorPool(pool.handle, 0);
}

auto Device::create_descriptor_set_layout(DescriptorSetLayoutDesc desc)
    -> DescriptorSetLayout {
  auto binding_flags =
      desc.bindings |
      map([](const DescriptorBinding &binding) { return binding.flags; }) |
      ranges::to<Vector>;

  auto bindings = desc.bindings | map([](const DescriptorBinding &binding) {
                    return VkDescriptorSetLayoutBinding{
                        .binding = binding.binding,
                        .descriptorType = binding.type,
                        .descriptorCount = binding.count,
                        .stageFlags = binding.stages,
                    };
                  }) |
                  ranges::to<Vector>;

  VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info = {
      .sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = unsigned(binding_flags.size()),
      .pBindingFlags = binding_flags.data(),
  };

  VkDescriptorSetLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = &binding_flags_info,
      .flags = desc.flags,
      .bindingCount = unsigned(bindings.size()),
      .pBindings = bindings.data(),
  };

  VkDescriptorSetLayout layout;
  throwIfFailed(CreateDescriptorSetLayout(&layout_info, &layout),
                "Vulkan: Failed to create descriptor set layout");

  return {.desc = std::make_shared<DescriptorSetLayoutDesc>(std::move(desc)),
          .handle = {layout, [this](VkDescriptorSetLayout layout) {
                       push_to_delete_queue(layout);
                     }}};
}

auto Device::allocate_descriptor_sets(
    const DescriptorPoolRef &pool,
    std::span<const DescriptorSetLayoutRef> layouts, VkDescriptorSet *sets)
    -> bool {
  auto vk_layouts = layouts |
                    map([](const auto &layout) { return layout.handle; }) |
                    ranges::to<SmallVector<VkDescriptorSetLayout>>;

  VkDescriptorSetAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = pool.handle,
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

auto Device::allocate_descriptor_set(const DescriptorPoolRef &pool,
                                     DescriptorSetLayoutRef layout)
    -> Optional<VkDescriptorSet> {
  VkDescriptorSet set;
  if (allocate_descriptor_sets(pool, {&layout, 1}, &set)) {
    return set;
  }
  return None;
}

auto Device::allocate_descriptor_set(DescriptorSetLayoutRef layout)
    -> std::pair<DescriptorPool, VkDescriptorSet> {
  DescriptorPoolDesc pool_desc = {.set_count = 1};
  if (layout.desc->flags &
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT) {
    pool_desc.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  }
  for (const auto &binding : layout.desc->bindings) {
    pool_desc.pool_sizes[binding.type] += binding.count;
  }
  auto pool = create_descriptor_pool(pool_desc);
  auto set = allocate_descriptor_set(pool, layout);
  assert(set);
  return {std::move(pool), *set};
}

void Device::write_descriptor_sets(
    std::span<const VkWriteDescriptorSet> configs) {
  UpdateDescriptorSets(configs.size(), configs.data(), 0, nullptr);
}

void Device::write_descriptor_set(const VkWriteDescriptorSet &config) {
  write_descriptor_sets({&config, 1});
}

auto Device::create_buffer(BufferDesc desc) -> Buffer {
  assert(desc.offset == 0);
  assert(!desc.ptr);
  if (desc.size == 0) {
    return {.desc = desc};
  }

  VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = desc.size,
      .usage = desc.usage,
  };

  VmaAllocationCreateInfo alloc_info = {
      .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO,
  };

  switch (desc.heap) {
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
  desc.ptr = reinterpret_cast<std::byte *>(map_info.pMappedData);
  if (desc.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
    VkBufferDeviceAddressInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer,
    };
    desc.address = GetBufferDeviceAddress(&buffer_info);
  }

  return {.desc = desc, .handle = {buffer, [this, allocation](VkBuffer buffer) {
                                     push_to_delete_queue(buffer);
                                     push_to_delete_queue(allocation);
                                   }}};
}

Texture Device::create_texture(TextureDesc desc) {
  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = desc.type,
      .format = desc.format,
      .extent = {desc.width, desc.height, desc.depth},
      .mipLevels = desc.mip_levels,
      .arrayLayers = desc.array_layers,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = desc.usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO};

  VkImage image;
  VmaAllocation allocation;
  throwIfFailed(vmaCreateImage(m_allocator, &image_info, &alloc_info, &image,
                               &allocation, nullptr),
                "VMA: Failed to create image");

  return {.desc = desc, .handle = {image, [this, allocation](VkImage image) {
                                     push_to_delete_queue(ImageViews{image});
                                     push_to_delete_queue(image);
                                     push_to_delete_queue(allocation);
                                   }}};
}

void Device::destroy_image_views(VkImage image) {
  for (auto &&[_, view] : m_image_views[image]) {
    DestroyImageView(view);
  }
  m_image_views.erase(image);
}

VkImageView Device::getVkImageView(const TextureView &view) {
  auto image = view.texture;
  if (!image) {
    return VK_NULL_HANDLE;
  }
  auto [it, inserted] = m_image_views[image].insert(view.desc, VK_NULL_HANDLE);
  auto &image_view = std::get<1>(*it);
  if (inserted) {
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = view.texture,
        .viewType = view.desc.type,
        .format = view.desc.format,
        .components =
            {
                .r = view.desc.components.r,
                .g = view.desc.components.g,
                .b = view.desc.components.b,
                .a = view.desc.components.a,
            },
        .subresourceRange =
            {
                .aspectMask = view.desc.aspects,
                .baseMipLevel = view.desc.first_mip_level,
                .levelCount = view.desc.mip_levels,
                .baseArrayLayer = view.desc.first_array_layer,
                .layerCount = view.desc.array_layers,
            },
    };
    throwIfFailed(CreateImageView(&view_info, &image_view),
                  "Vulkan: Failed to create image view");
  }
  return image_view;
}

auto Device::createBinarySemaphore() -> Semaphore {
  VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };
  VkSemaphore semaphore;
  throwIfFailed(CreateSemaphore(&semaphore_info, &semaphore),
                "Vulkan: Failed to create binary semaphore");
  return {.handle = {semaphore, [this](VkSemaphore semaphore) {
                       push_to_delete_queue(semaphore);
                     }}};
}

auto Device::createTimelineSemaphore(uint64_t initial_value) -> VkSemaphore {
  VkSemaphoreTypeCreateInfo semaphore_type_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = initial_value,
  };
  VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &semaphore_type_info,
  };
  VkSemaphore semaphore;
  throwIfFailed(CreateSemaphore(&semaphore_info, &semaphore),
                "Vulkan: Failed to create timeline semaphore");
  return semaphore;
}

SemaphoreWaitResult
Device::waitForSemaphore(VkSemaphore sem, uint64_t value,
                         std::chrono::nanoseconds timeout) const {
  VkSemaphoreWaitInfo wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &sem,
      .pValues = &value,
  };
  auto r = WaitSemaphores(&wait_info, timeout.count());
  switch (r) {
    using enum SemaphoreWaitResult;
  case VK_SUCCESS:
    return Ready;
  case VK_TIMEOUT:
    return Timeout;
  default:
    throw std::runtime_error{"Vulkan: Failed to wait for semaphore"};
  };
}

auto Device::getSemaphoreValue(VkSemaphore semaphore) const -> uint64_t {
  uint64_t value;
  throwIfFailed(GetSemaphoreCounterValue(semaphore, &value),
                "Vulkan: Failed to get semaphore value");
  return value;
}

void Device::queueSubmitAndSignal(VkQueue queue,
                                  std::span<const Submit> submits,
                                  VkSemaphore semaphore, uint64_t value) {
  auto submit_infos =
      submits | map([](const Submit &submit) {
        return VkSubmitInfo2{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = uint32_t(submit.wait_semaphores.size()),
            .pWaitSemaphoreInfos = submit.wait_semaphores.data(),
            .commandBufferInfoCount = uint32_t(submit.command_buffers.size()),
            .pCommandBufferInfos = submit.command_buffers.data(),
            .signalSemaphoreInfoCount =
                uint32_t(submit.signal_semaphores.size()),
            .pSignalSemaphoreInfos = submit.signal_semaphores.data(),
        };
      }) |
      ranges::to<SmallVector<VkSubmitInfo2, 8>>;

  auto final_signal_semaphores =
      concat(submits.back().signal_semaphores,
             once(VkSemaphoreSubmitInfo{
                 .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                 .semaphore = semaphore,
                 .value = value,
             })) |
      ranges::to<SmallVector<VkSemaphoreSubmitInfo, 8>>;

  auto &final_submit_info = submit_infos.back();
  final_submit_info.signalSemaphoreInfoCount = final_signal_semaphores.size();
  final_submit_info.pSignalSemaphoreInfos = final_signal_semaphores.data();

  throwIfFailed(QueueSubmit2(queue, submit_infos.size(), submit_infos.data(),
                             VK_NULL_HANDLE),
                "Vulkan: Failed to submit work to queue");
}

auto Device::create_shader_module(std::span<const std::byte> code)
    -> SharedHandle<VkShaderModule> {
  VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code.size(),
      .pCode = reinterpret_cast<const uint32_t *>(code.data()),
  };
  VkShaderModule module;
  throwIfFailed(CreateShaderModule(&module_info, &module),
                "Vulkan: Failed to create shader module");
  return {module,
          [this](VkShaderModule module) { DestroyShaderModule(module); }};
}

auto Device::create_graphics_pipeline(GraphicsPipelineConfig config)
    -> GraphicsPipeline {
  for (auto &&[entry_point, shader] :
       zip(config.entry_points, config.shaders)) {
    shader.pName = entry_point.c_str();
  }

  VkPipelineVertexInputStateCreateInfo vertex_input_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  VkPipelineViewportStateCreateInfo viewport_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
  };

  config.desc.blend_info.attachmentCount =
      config.desc.render_target_blend_infos.size();
  config.desc.blend_info.pAttachments =
      config.desc.render_target_blend_infos.data();

  config.desc.dynamic_state_info.dynamicStateCount =
      config.desc.dynamic_states.size();
  config.desc.dynamic_state_info.pDynamicStates =
      config.desc.dynamic_states.data();

  config.desc.rendering_info.colorAttachmentCount =
      config.desc.render_target_formats.size();
  config.desc.rendering_info.pColorAttachmentFormats =
      config.desc.render_target_formats.data();

  VkGraphicsPipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &config.desc.rendering_info,
      .stageCount = unsigned(config.shaders.size()),
      .pStages = config.shaders.data(),
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &config.desc.input_assembly_info,
      .pViewportState = &viewport_info,
      .pRasterizationState = &config.desc.rasterization_info,
      .pMultisampleState = &config.desc.multisample_info,
      .pColorBlendState = &config.desc.blend_info,
      .pDynamicState = &config.desc.dynamic_state_info,
      .layout = config.layout.handle,
  };

  VkPipeline pipeline;
  throwIfFailed(CreateGraphicsPipelines(nullptr, 1, &pipeline_info, &pipeline),
                "Vulkan: Failed to create graphics pipeline");

  return {
      .desc = std::make_shared<GraphicsPipelineDesc>(std::move(config.desc)),
      .handle = {pipeline,
                 [this](VkPipeline pipeline) {
                   push_to_delete_queue(pipeline);
                 }},
  };
}

auto Device::create_pipeline_layout(PipelineLayoutDesc desc) -> PipelineLayout {
  auto set_layouts =
      desc.set_layouts |
      map([](const auto &layout) { return layout.handle.get(); }) |
      ranges::to<SmallVector<VkDescriptorSetLayout, 4>>;

  VkPipelineLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = unsigned(set_layouts.size()),
      .pSetLayouts = set_layouts.data(),
      .pushConstantRangeCount = unsigned(desc.push_constants.size()),
      .pPushConstantRanges = desc.push_constants.data(),
  };

  VkPipelineLayout layout;
  throwIfFailed(CreatePipelineLayout(&layout_info, &layout),
                "Vulkan: Failed to create pipeline layout");

  return {.desc = std::make_unique<PipelineLayoutDesc>(std::move(desc)),
          .handle = {layout, [this](VkPipelineLayout layout) {
                       push_to_delete_queue(layout);
                     }}};
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
    VkSemaphoreSubmitInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = getGraphicsQueueSemaphore(),
        .value = ++m_graphics_queue_time,
    };
    VkSubmitInfo2 submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &semaphore_info,
    };
    throwIfFailed(QueueSubmit2(queue, 1, &submit_info, VK_NULL_HANDLE),
                  "Vulkan: Failed to submit semaphore signal operation");
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
