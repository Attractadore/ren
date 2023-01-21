#include "Vulkan/VulkanDevice.hpp"
#include "Support/Array.hpp"
#include "Support/Variant.hpp"
#include "Support/Views.hpp"
#include "Vulkan/VulkanBuffer.hpp"
#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Vulkan/VulkanDeleteQueue.inl"
#include "Vulkan/VulkanDescriptors.hpp"
#include "Vulkan/VulkanErrors.hpp"
#include "Vulkan/VulkanFormats.hpp"
#include "Vulkan/VulkanPipeline.hpp"
#include "Vulkan/VulkanReflection.hpp"
#include "Vulkan/VulkanRenderGraph.hpp"
#include "Vulkan/VulkanShaderStages.hpp"
#include "Vulkan/VulkanSwapchain.hpp"
#include "Vulkan/VulkanTexture.hpp"

constexpr bool operator==(const VkImageViewCreateInfo &lhs,
                          const VkImageViewCreateInfo &rhs) {
  return lhs.flags == rhs.flags and lhs.viewType == rhs.viewType and
         lhs.format == rhs.format and
         lhs.subresourceRange.aspectMask == rhs.subresourceRange.aspectMask and
         lhs.subresourceRange.baseMipLevel ==
             rhs.subresourceRange.baseMipLevel and
         lhs.subresourceRange.levelCount == rhs.subresourceRange.levelCount and
         lhs.subresourceRange.baseArrayLayer ==
             rhs.subresourceRange.baseArrayLayer and
         lhs.subresourceRange.layerCount == rhs.subresourceRange.layerCount;
}

namespace ren {
std::span<const char *const> VulkanDevice::getRequiredLayers() {
  static constexpr auto layers = makeArray<const char *>(
#if REN_VULKAN_VALIDATION
      "VK_LAYER_KHRONOS_validation"
#endif
  );
  return layers;
}

std::span<const char *const> VulkanDevice::getRequiredExtensions() {
  static constexpr auto extensions = makeArray<const char *>();
  return extensions;
}

namespace {
int findQueueFamilyWithCapabilities(VulkanDevice *device, VkQueueFlags caps) {
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

int findGraphicsQueueFamily(VulkanDevice *device) {
  return findQueueFamilyWithCapabilities(device, VK_QUEUE_GRAPHICS_BIT |
                                                     VK_QUEUE_COMPUTE_BIT |
                                                     VK_QUEUE_TRANSFER_BIT);
}
} // namespace

VulkanDevice::VulkanDevice(PFN_vkGetInstanceProcAddr proc, VkInstance instance,
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

VulkanDevice::~VulkanDevice() {
  waitForIdle();
  m_delete_queue.flush(*this);
  DestroySemaphore(m_graphics_queue_semaphore);
  vmaDestroyAllocator(m_allocator);
  DestroyDevice();
}

auto VulkanDevice::supports_feature(DeviceFeature feature) const -> bool {
  switch (feature) {
    using enum DeviceFeature;
  default:
    return false;
  case BufferDeviceAddress:
    return true;
  }
}

void VulkanDevice::begin_frame() {
  m_frame_index = (m_frame_index + 1) % m_frame_end_times.size();
  waitForGraphicsQueue(m_frame_end_times[m_frame_index].graphics_queue_time);
  m_delete_queue.begin_frame(*this);
}

void VulkanDevice::end_frame() {
  m_delete_queue.end_frame(*this);
  m_frame_end_times[m_frame_index].graphics_queue_time = getGraphicsQueueTime();
}

auto VulkanDevice::create_command_allocator(QueueType queue_type)
    -> std::unique_ptr<CommandAllocator> {
  return std::make_unique<VulkanCommandAllocator>(*this);
}

auto VulkanDevice::create_descriptor_pool(const DescriptorPoolDesc &desc)
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
      .flags = getVkDescriptorPoolOptionFlags(desc.flags),
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

void VulkanDevice::reset_descriptor_pool(const DescriptorPoolRef &pool) {
  ResetDescriptorPool(getVkDescriptorPool(pool), 0);
}

auto VulkanDevice::create_descriptor_set_layout(
    const DescriptorSetLayoutDesc &desc) -> DescriptorSetLayout {
  auto binding_flags =
      desc.bindings | map([](const DescriptorBinding &binding) {
        return getVkDescriptorBindingOptionFlags(binding.flags);
      }) |
      ranges::to<Vector>;

  auto bindings = desc.bindings | map([](const DescriptorBinding &binding) {
                    return VkDescriptorSetLayoutBinding{
                        .binding = binding.binding,
                        .descriptorType = binding.type,
                        .descriptorCount = binding.count,
                        .stageFlags = getVkShaderStageFlags(binding.stages),
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
      .flags = getVkDescriptorSetLayoutOptionFlags(desc.flags),
      .bindingCount = unsigned(bindings.size()),
      .pBindings = bindings.data(),
  };

  VkDescriptorSetLayout layout;
  throwIfFailed(CreateDescriptorSetLayout(&layout_info, &layout),
                "Vulkan: Failed to create descriptor set layout");

  return {.desc = std::make_shared<DescriptorSetLayoutDesc>(desc),
          .handle = {layout, [this](VkDescriptorSetLayout layout) {
                       push_to_delete_queue(layout);
                     }}};
}

auto VulkanDevice::allocate_descriptor_sets(
    const DescriptorPoolRef &pool,
    std::span<const DescriptorSetLayoutRef> layouts,
    std::span<VkDescriptorSet> sets) -> bool {
  assert(sets.size() >= layouts.size());

  auto vk_layouts = layouts | map(getVkDescriptorSetLayout) |
                    ranges::to<SmallVector<VkDescriptorSetLayout>>;
  SmallVector<VkDescriptorSet> vk_sets(vk_layouts.size());

  VkDescriptorSetAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = getVkDescriptorPool(pool),
      .descriptorSetCount = unsigned(vk_layouts.size()),
      .pSetLayouts = vk_layouts.data(),
  };

  auto result = AllocateDescriptorSets(&alloc_info, sets.data());
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

void VulkanDevice::write_descriptor_sets(
    std::span<const VkWriteDescriptorSet> configs) {
  UpdateDescriptorSets(configs.size(), configs.data(), 0, nullptr);
}

auto VulkanDevice::create_buffer_handle(const BufferDesc &desc)
    -> std::pair<SharedHandle<VkBuffer>, void *> {
  VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = desc.size,
      .usage = getVkBufferUsageFlags(desc.usage),
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

  return {
      {buffer,
       [this, allocation](VkBuffer buffer) {
         push_to_delete_queue(buffer);
         push_to_delete_queue(allocation);
       }},
      map_info.pMappedData,
  };
}

auto VulkanDevice::get_buffer_device_address(const BufferRef &buffer) const
    -> uint64_t {
  VkBufferDeviceAddressInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = getVkBuffer(buffer),
  };
  return GetBufferDeviceAddress(&buffer_info);
}

Texture VulkanDevice::createTexture(const TextureDesc &desc) {
  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = getVkImageType(desc.type),
      .format = getVkFormat(desc.format),
      .extent = {desc.width, desc.height, desc.depth},
      .mipLevels = desc.mip_levels,
      .arrayLayers = desc.array_layers,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = getVkImageUsageFlags(desc.usage),
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO};

  VkImage image;
  VmaAllocation allocation;
  throwIfFailed(vmaCreateImage(m_allocator, &image_info, &alloc_info, &image,
                               &allocation, nullptr),
                "VMA: Failed to create image");

  return {.desc = desc,
          .handle = {image, [this, allocation](VkImage image) {
                       push_to_delete_queue(VulkanImageViews{image});
                       push_to_delete_queue(image);
                       push_to_delete_queue(allocation);
                     }}};
}

void VulkanDevice::destroyImageViews(VkImage image) {
  for (auto &&[_, view] : m_image_views[image]) {
    DestroyImageView(view);
  }
  m_image_views.erase(image);
}

VkImageView VulkanDevice::getVkImageView(const RenderTargetView &rtv) {
  auto image = getVkImage(rtv.texture);
  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = getVkFormat(rtv.desc.format),
      .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = rtv.desc.mip_level,
          .levelCount = 1,
          .baseArrayLayer = rtv.desc.array_layer,
          .layerCount = 1,
      }};
  return getVkImageViewImpl(image, view_info);
}

VkImageView VulkanDevice::getVkImageView(const DepthStencilView &dsv) {
  auto image = getVkImage(dsv.texture);
  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = getVkFormat(dsv.texture.desc.format),
      .subresourceRange = {
          .aspectMask = getVkImageAspectFlags(dsv.texture.desc.format),
          .baseMipLevel = dsv.desc.mip_level,
          .levelCount = 1,
          .baseArrayLayer = dsv.desc.array_layer,
          .layerCount = 1,
      }};
  return getVkImageViewImpl(image, view_info);
}

VkImageView
VulkanDevice::getVkImageViewImpl(VkImage image,
                                 const VkImageViewCreateInfo &view_info) {
  if (!image) {
    return VK_NULL_HANDLE;
  }
  auto [it, inserted] = m_image_views[image].insert(view_info, VK_NULL_HANDLE);
  auto &view = std::get<1>(*it);
  if (inserted) {
    throwIfFailed(CreateImageView(&view_info, &view),
                  "Vulkan: Failed to create image view");
  }
  return view;
}

auto VulkanDevice::createBinarySemaphore() -> Semaphore {
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

auto VulkanDevice::createTimelineSemaphore(uint64_t initial_value)
    -> VkSemaphore {
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
VulkanDevice::waitForSemaphore(VkSemaphore sem, uint64_t value,
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

std::unique_ptr<RenderGraph::Builder> VulkanDevice::createRenderGraphBuilder() {
  return std::make_unique<VulkanRenderGraph::Builder>(*this);
}

std::unique_ptr<VulkanSwapchain>
VulkanDevice::createSwapchain(VkSurfaceKHR surface) {
  return std::make_unique<VulkanSwapchain>(this, surface);
}

void VulkanDevice::queueSubmitAndSignal(VkQueue queue,
                                        std::span<const VulkanSubmit> submits,
                                        VkSemaphore semaphore, uint64_t value) {
  auto submit_infos =
      submits | map([](const VulkanSubmit &submit) {
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

namespace {
VkShaderModule create_shader_module(VulkanDevice &device,
                                    std::span<const std::byte> code) {
  VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code.size_bytes(),
      .pCode = reinterpret_cast<const uint32_t *>(code.data()),
  };
  VkShaderModule module;
  throwIfFailed(device.CreateShaderModule(&module_info, &module),
                "Vulkan: Failed to create shader module");
  return module;
}
} // namespace

auto VulkanDevice::create_graphics_pipeline_handle(
    const GraphicsPipelineConfig &config) -> SharedHandle<VkPipeline> {
  SmallVector<VkDynamicState> dynamic_states = {
      VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
      VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
  };

  auto rt_format = getVkFormat(config.desc.rt.format);

  VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &rt_format,
  };

  auto modules =
      config.shaders | map([&](const ShaderStageConfig &shader) {
        VkShaderModuleCreateInfo module_info = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = shader.code.size_bytes(),
            .pCode = reinterpret_cast<const uint32_t *>(shader.code.data()),
        };
        VkShaderModule module;
        throwIfFailed(CreateShaderModule(&module_info, &module),
                      "Vulkan: Failed to create shader module");
        return module;
      }) |
      ranges::to<SmallVector<VkShaderModule, 8>>;

  auto stages =
      ranges::views::zip(config.shaders, modules) | map([](const auto &p) {
        const auto &[shader, module] = p;
        return VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = getVkShaderStage(shader.stage),
            .module = module,
            .pName = shader.entry_point.c_str(),
        };
      }) |
      ranges::to<SmallVector<VkPipelineShaderStageCreateInfo, 5>>;

  auto bindings = config.desc.ia.bindings |
                  map([](const VertexBinding &binding) {
                    return VkVertexInputBindingDescription{
                        .binding = binding.binding,
                        .stride = binding.stride,
                        .inputRate = getVkVertexInputRate(binding.rate),
                    };
                  }) |
                  ranges::to<SmallVector<VkVertexInputBindingDescription, 32>>;

  SmallVector<VkVertexInputAttributeDescription, 32> attributes;
  for (const auto &attribute : config.desc.ia.attributes) {
    auto format = getVkFormat(attribute.format);
    auto format_size = get_format_size(attribute.format);
    for (int i = 0; i < attribute.count; ++i) {
      attributes.push_back({
          .location = attribute.location + i,
          .binding = attribute.binding,
          .format = format,
          .offset = attribute.offset + i * format_size,
      });
    }
  }

  VkPipelineVertexInputStateCreateInfo vertex_input_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = unsigned(bindings.size()),
      .pVertexBindingDescriptions = bindings.data(),
      .vertexAttributeDescriptionCount = unsigned(attributes.size()),
      .pVertexAttributeDescriptions = attributes.data(),
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = std::visit(
          [](auto topology) { return getVkPrimitiveTopology(topology); },
          config.desc.ia.topology),
  };
  if (std::holds_alternative<PrimitiveTopologyType>(config.desc.ia.topology)) {
    dynamic_states.push_back(VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY);
  }

  VkPipelineViewportStateCreateInfo viewport_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .lineWidth = 1.0f,
  };

  VkSampleMask mask = config.desc.ms.sample_mask;
  VkPipelineMultisampleStateCreateInfo multisample_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VkSampleCountFlagBits(config.desc.ms.samples),
      .pSampleMask = &mask,
  };

  VkPipelineColorBlendAttachmentState blend_attachment_info = {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };

  VkPipelineColorBlendStateCreateInfo blend_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &blend_attachment_info,
  };

  VkPipelineDynamicStateCreateInfo dynamic_state_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = unsigned(dynamic_states.size()),
      .pDynamicStates = dynamic_states.data(),
  };

  VkGraphicsPipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = unsigned(stages.size()),
      .pStages = stages.data(),
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &input_assembly_info,
      .pViewportState = &viewport_info,
      .pRasterizationState = &rasterization_info,
      .pMultisampleState = &multisample_info,
      .pColorBlendState = &blend_info,
      .pDynamicState = &dynamic_state_info,
      .layout = getVkPipelineLayout(config.signature),
  };

  VkPipeline pipeline;
  throwIfFailed(CreateGraphicsPipelines(nullptr, 1, &pipeline_info, &pipeline),
                "Vulkan: Failed to create graphics pipeline");

  for (auto module : modules) {
    DestroyShaderModule(module);
  }

  return {pipeline,
          [this](VkPipeline pipeline) { push_to_delete_queue(pipeline); }};
}

auto VulkanDevice::create_reflection_module(std::span<const std::byte> data)
    -> std::unique_ptr<ReflectionModule> {
  return std::make_unique<VulkanReflectionModule>(data);
}

auto VulkanDevice::create_pipeline_signature(const PipelineSignatureDesc &desc)
    -> PipelineSignature {
  auto set_layouts = desc.set_layouts | map(getVkDescriptorSetLayout) |
                     ranges::to<SmallVector<VkDescriptorSetLayout, 4>>;

  auto pc_ranges = desc.push_constants |
                   map([](const PushConstantRange &pc_range) {
                     return VkPushConstantRange{
                         .stageFlags = getVkShaderStageFlags(pc_range.stages),
                         .offset = pc_range.offset,
                         .size = pc_range.size,
                     };
                   }) |
                   ranges::to<SmallVector<VkPushConstantRange, 4>>;

  VkPipelineLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = unsigned(set_layouts.size()),
      .pSetLayouts = set_layouts.data(),
      .pushConstantRangeCount = unsigned(pc_ranges.size()),
      .pPushConstantRanges = pc_ranges.data(),
  };

  VkPipelineLayout layout;
  throwIfFailed(CreatePipelineLayout(&layout_info, &layout),
                "Vulkan: Failed to create pipeline layout");

  return {.desc = std::make_unique<PipelineSignatureDesc>(desc),
          .handle = {layout, [this](VkPipelineLayout layout) {
                       push_to_delete_queue(layout);
                     }}};
}

} // namespace ren
