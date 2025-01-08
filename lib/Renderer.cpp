#include "Renderer.hpp"
#include "Formats.hpp"
#include "Profiler.hpp"
#include "Scene.hpp"
#include "Swapchain.hpp"
#include "core/Errors.hpp"
#include "core/Views.hpp"

#include <spirv_reflect.h>
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

auto Renderer::init(u32 adapter) -> Result<void, Error> {
  ren_try(rhi::Features features, rhi::get_supported_features());

#if !REN_DEBUG_NAMES
  features.debug_names = false;
#endif

#if !REN_DEBUG_LAYER
  features.debug_layer = false;
#endif

  ren_try_to(rhi::init({.features = features}));

  if (adapter == DEFAULT_ADAPTER) {
    m_adapter =
        rhi::get_adapter_by_preference(rhi::AdapterPreference::HighPerformance);
  } else {
    if (adapter >= rhi::get_adapter_count()) {
      throw std::runtime_error("Vulkan: Failed to find requested adapter");
    }
    m_adapter = rhi::get_adapter(adapter);
  }

  ren_try(m_device, rhi::create_device({
                        .adapter = m_adapter,
                        .features = rhi::get_adapter_features(m_adapter),
                    }));

  m_graphics_queue = rhi::get_queue(m_device, rhi::QueueFamily::Graphics);

  return {};
}

Renderer::~Renderer() {
  wait_idle();
  rhi::destroy_device(m_device);
  rhi::exit();
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
    vkDestroyDescriptorPool(get_device(), pool.handle, nullptr);
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
        vkDestroyDescriptorSetLayout(get_device(), layout.handle, nullptr);
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
    -> Result<Handle<Buffer>, Error> {
  ren_assert(create_info.size > 0);
  ren_try(rhi::Buffer buffer, rhi::create_buffer({
                                  .device = m_device,
                                  .size = create_info.size,
                                  .heap = create_info.heap,
                              }));
  set_debug_name(get_device(), buffer.handle, create_info.name);
  return m_buffers.emplace(Buffer{
      .handle = buffer,
      .ptr = (std::byte *)rhi::map(m_device, buffer),
      .address = rhi::get_device_ptr(m_device, buffer),
      .size = create_info.size,
      .heap = create_info.heap,
  });
}

void Renderer::destroy(Handle<Buffer> handle) {
  m_buffers.try_pop(handle).map([&](const Buffer &buffer) {
    rhi::destroy_buffer(m_device, buffer.handle);
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
    -> Result<Handle<Texture>, Error> {
  ren_assert(create_info.width > 0);
  ren_assert(create_info.num_mip_levels > 0);
  ren_assert(create_info.num_array_layers > 0);

  ren_try(rhi::Image image,
          rhi::create_image({
              .device = m_device,
              .format = create_info.format,
              .width = create_info.width,
              .height = create_info.height,
              .depth = create_info.depth,
              .num_mip_levels = create_info.num_mip_levels,
              .num_array_layers = create_info.num_array_layers,
              .usage = create_info.usage,
          }));
  set_debug_name(get_device(), image.handle, create_info.name);

  return m_textures.emplace(Texture{
      .handle = image,
      .format = create_info.format,
      .usage = create_info.usage,
      .width = create_info.width,
      .height = create_info.height,
      .depth = create_info.depth,
      .num_mip_levels = create_info.num_mip_levels,
      .num_array_layers = create_info.num_array_layers,
  });
}

auto Renderer::create_external_texture(
    const ExternalTextureCreateInfo &&create_info) -> Handle<Texture> {
  set_debug_name(get_device(), create_info.handle.handle, create_info.name);
  return m_textures.emplace(Texture{
      .handle = create_info.handle,
      .format = create_info.format,
      .usage = create_info.usage,
      .width = create_info.width,
      .height = create_info.height,
      .depth = create_info.depth,
      .num_mip_levels = create_info.num_mip_levels,
      .num_array_layers = create_info.num_array_layers,
  });
}

void Renderer::destroy(Handle<Texture> handle) {
  m_textures.try_pop(handle).map([&](const Texture &texture) {
    if (rhi::get_allocation(m_device, texture.handle)) {
      rhi::destroy_image(m_device, texture.handle);
    }
    for (const auto &[_, view] : m_image_views[handle]) {
      vkDestroyImageView(get_device(), view, nullptr);
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

namespace {

static auto get_texture_default_view_type(const Texture &texture)
    -> VkImageViewType {
  if (texture.num_array_layers > 1) {
    if (texture.height > 0) {
      return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    }
    ren_assert(texture.depth == 0);
    return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
  }
  if (texture.depth > 0) {
    return VK_IMAGE_VIEW_TYPE_3D;
  }
  if (texture.height > 0) {
    return VK_IMAGE_VIEW_TYPE_2D;
  }
  return VK_IMAGE_VIEW_TYPE_1D;
}

} // namespace

auto Renderer::try_get_texture_view(Handle<Texture> handle) const
    -> Optional<TextureView> {
  return try_get_texture(handle).map(
      [&](const Texture &texture) -> TextureView {
        return {
            .texture = handle,
            .type = get_texture_default_view_type(texture),
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
      .type = get_texture_default_view_type(texture),
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
      .image = get_texture(view.texture).handle.handle,
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
    vkDestroySampler(get_device(), sampler.handle, nullptr);
  });
}

auto Renderer::get_sampler(Handle<Sampler> sampler) const -> const Sampler & {
  ren_assert(m_samplers.contains(sampler));
  return m_samplers[sampler];
}

auto Renderer::create_semaphore(const SemaphoreCreateInfo &&create_info)
    -> Result<Handle<Semaphore>, Error> {
  ren_try(rhi::Semaphore semaphore,
          rhi::create_semaphore({
              .device = m_device,
              .type = create_info.type,
              .initial_value = create_info.initial_value,
          }));
  return m_semaphores.emplace(Semaphore{.handle = semaphore});
}

void Renderer::destroy(Handle<Semaphore> semaphore) {
  m_semaphores.try_pop(semaphore).map([&](const Semaphore &semaphore) {
    rhi::destroy_semaphore(m_device, semaphore.handle);
  });
}

auto Renderer::wait_for_semaphore(Handle<Semaphore> semaphore, u64 value,
                                  std::chrono::nanoseconds timeout) const
    -> Result<rhi::WaitResult, Error> {
  return rhi::wait_for_semaphores(
      m_device, {{get_semaphore(semaphore).handle, value}}, timeout);
}

auto Renderer::wait_for_semaphore(Handle<Semaphore> semaphore, u64 value) const
    -> Result<void, Error> {
  ren_try(rhi::WaitResult wait_result,
          wait_for_semaphore(semaphore, value,
                             std::chrono::nanoseconds(UINT64_MAX)));
  ren_assert(wait_result == rhi::WaitResult::Success);
  return {};
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
    rhi::Queue queue, TempSpan<const VkCommandBufferSubmitInfo> cmd_buffers,
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
  throw_if_failed(vkQueueSubmit2(queue.handle, 1, &submit_info, nullptr),
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
        vkDestroyPipeline(get_device(), pipeline.handle, nullptr);
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
  Span<const std::byte> code = create_info.shader.code;

  spv_reflect::ShaderModule shader(code.size_bytes(), code.data(),
                                   SPV_REFLECT_MODULE_FLAG_NO_COPY);
  throw_if_failed(shader.GetResult(),
                  "SPIRV-Reflect: Failed to create shader module");
  const SpvReflectEntryPoint *entry_point = spvReflectGetEntryPoint(
      &shader.GetShaderModule(), create_info.shader.entry_point);
  throw_if_failed(entry_point,
                  "SPIRV-Reflect: Failed to find entry point in shader module");
  SpvReflectEntryPoint::LocalSize local_size = entry_point->local_size;

  VkShaderModule module = create_shader_module(get_device(), code);

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
      .local_size = {local_size.x, local_size.y, local_size.z},
  });
}

void Renderer::destroy(Handle<ComputePipeline> pipeline) {
  m_compute_pipelines.try_pop(pipeline).map(
      [&](const ComputePipeline &pipeline) {
        vkDestroyPipeline(get_device(), pipeline.handle, nullptr);
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
    vkDestroyPipelineLayout(get_device(), layout.handle, nullptr);
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
  return vkQueuePresentKHR(getGraphicsQueue().handle, &present_info);
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
  vkAntiLagUpdateAMD(get_device(), &anti_lag_data);
}

} // namespace ren
