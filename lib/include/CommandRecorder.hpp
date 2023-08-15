#pragma once
#include "Attachments.hpp"
#include "Descriptors.hpp"
#include "Pipeline.hpp"
#include "Support/Errors.hpp"
#include "Support/Optional.hpp"
#include "Support/Span.hpp"

#include <glm/glm.hpp>

namespace ren {

auto get_num_dispatch_groups(u32 size, u32 group_size) -> u32;

auto get_num_dispatch_groups(glm::uvec2 size, glm::uvec2 group_size)
    -> glm::uvec2;

auto get_num_dispatch_groups(glm::uvec3 size, glm::uvec3 group_size)
    -> glm::uvec3;

class Device;

struct ColorAttachment {
  TextureView texture;
  ColorAttachmentOperations ops;
};

struct DepthStencilAttachment {
  TextureView texture;
  Optional<DepthAttachmentOperations> depth_ops;
  Optional<StencilAttachmentOperations> stencil_ops;
};

struct RenderPassBeginInfo {
  TempSpan<const Optional<ColorAttachment>> color_attachments;
  Optional<DepthStencilAttachment> depth_stencil_attachment;
};

class RenderPass;

struct DrawIndexedInfo {
  unsigned num_indices = 0;
  unsigned num_instances = 1;
  unsigned first_index = 0;
  int vertex_offset = 0;
  unsigned first_instance = 0;
};

class ComputePass;

class DebugRegion;

class CommandRecorder {
  Device *m_device;
  VkCommandBuffer m_cmd_buffer;

public:
  CommandRecorder(Device &device, VkCommandBuffer cmd_buffer);
  CommandRecorder(const CommandRecorder &) = delete;
  CommandRecorder(CommandRecorder &&) = delete;
  ~CommandRecorder();

  CommandRecorder &operator=(const CommandRecorder &) = delete;
  CommandRecorder &operator=(CommandRecorder &&) = delete;

  auto render_pass(const RenderPassBeginInfo &&begin_info) -> RenderPass;

  auto compute_pass() -> ComputePass;

  void copy_buffer(Handle<Buffer> src, Handle<Buffer> dst,
                   TempSpan<const VkBufferCopy> regions);

  void copy_buffer(const BufferView &src, const BufferView &dst);

  void copy_buffer_to_image(Handle<Buffer> src, Handle<Texture> dst,
                            TempSpan<const VkBufferImageCopy> regions);

  void fill_buffer(const BufferView &buffer, u32 value);

  template <typename T>
    requires(sizeof(T) == sizeof(u32))
  void fill_buffer(const BufferView &buffer, T value) {
    fill_buffer(buffer, std::bit_cast<u32>(value));
  }

  template <typename T>
  void update_buffer(const BufferView &buffer, TempSpan<const T> data) {
    update_buffer(buffer, data.as_bytes());
  }

  template <>
  void update_buffer<std::byte>(const BufferView &buffer,
                                TempSpan<const std::byte> data);

  template <typename T>
    requires(sizeof(T) % 4 == 0)
  void update_buffer(const BufferView &buffer, const T &data) {
    update_buffer<T>(buffer, TempSpan(&data, 1));
  }

  void blit(Handle<Texture> src, Handle<Texture> dst,
            TempSpan<const VkImageBlit> regions, VkFilter filter);

  void pipeline_barrier(const VkDependencyInfo &dependency_info);

  void pipeline_barrier(TempSpan<const VkMemoryBarrier2> barriers,
                        TempSpan<const VkImageMemoryBarrier2> image_barriers);

  auto debug_region(const char *label) -> DebugRegion;
};

class RenderPass {
  Device *m_device;
  VkCommandBuffer m_cmd_buffer;
  Handle<PipelineLayout> m_pipeline_layout;
  VkShaderStageFlags m_shader_stages = 0;

  friend class CommandRecorder;
  RenderPass(Device &device, VkCommandBuffer cmd_buffer,
             const RenderPassBeginInfo &&begin_info);

public:
  RenderPass(const RenderPass &) = delete;
  RenderPass(RenderPass &&) = delete;
  ~RenderPass();

  RenderPass &operator=(const RenderPass &) = delete;
  RenderPass &operator=(RenderPass &&) = delete;

  void set_viewports(StaticVector<VkViewport, MAX_COLOR_ATTACHMENTS> viewports);

  void set_scissor_rects(TempSpan<const VkRect2D> rects);

  void bind_graphics_pipeline(Handle<GraphicsPipeline> pipeline);

  void bind_descriptor_sets(Handle<PipelineLayout> layout,
                            TempSpan<const VkDescriptorSet> sets,
                            unsigned first_set = 0);

  void bind_descriptor_sets(TempSpan<const VkDescriptorSet> sets,
                            unsigned first_set = 0);

  void set_push_constants(Handle<PipelineLayout> layout,
                          VkShaderStageFlags stages,
                          TempSpan<const std::byte> data, unsigned offset = 0);

  void set_push_constants(Handle<PipelineLayout> layout,
                          VkShaderStageFlags stages, const auto &data,
                          unsigned offset = 0) {
    set_push_constants(layout, stages, TempSpan(&data, 1).as_bytes(), offset);
  }

  void set_push_constants(TempSpan<const std::byte> data, unsigned offset = 0);

  void set_push_constants(const auto &data, unsigned offset = 0) {
    ren_assert(m_pipeline_layout, "A graphics pipeline must be bound");
    set_push_constants(m_pipeline_layout, m_shader_stages,
                       TempSpan(&data, 1).as_bytes(), offset);
  }

  void bind_index_buffer(const BufferView &buffer, VkIndexType type);

  void draw_indexed(const DrawIndexedInfo &&draw_info);
};

class ComputePass {
  Device *m_device;
  VkCommandBuffer m_cmd_buffer;
  Handle<PipelineLayout> m_pipeline_layout;

  friend class CommandRecorder;
  ComputePass(Device &device, VkCommandBuffer cmd_buffer);

public:
  ComputePass(const ComputePass &) = delete;
  ComputePass(ComputePass &&) = delete;
  ~ComputePass();

  ComputePass &operator=(const ComputePass &) = delete;
  ComputePass &operator=(ComputePass &&) = delete;

  void bind_compute_pipeline(Handle<ComputePipeline> pipeline);

  void bind_descriptor_sets(Handle<PipelineLayout> layout,
                            TempSpan<const VkDescriptorSet> sets,
                            unsigned first_set = 0);

  void bind_descriptor_sets(TempSpan<const VkDescriptorSet> sets,
                            unsigned first_set = 0);

  void set_push_constants(Handle<PipelineLayout> layout,
                          TempSpan<const std::byte> data, unsigned offset = 0);

  void set_push_constants(TempSpan<const std::byte> data, unsigned offset = 0);

  void set_push_constants(Handle<PipelineLayout> layout, const auto &data,
                          unsigned offset = 0) {
    set_push_constants(layout, TempSpan(&data, 1).as_bytes(), offset);
  }

  void set_push_constants(const auto &data, unsigned offset = 0) {
    ren_assert(m_pipeline_layout, "A compute pipeline must be bound");
    set_push_constants(m_pipeline_layout, TempSpan(&data, 1).as_bytes(),
                       offset);
  }

  void dispatch_groups(u32 num_groups_x, u32 num_groups_y = 1,
                       u32 num_groups_z = 1);
  void dispatch_groups(glm::uvec2 num_groups);
  void dispatch_groups(glm::uvec3 num_groups);

  void dispatch_threads(u32 size, u32 group_size);
  void dispatch_threads(glm::uvec2 size, glm::uvec2 group_size);
  void dispatch_threads(glm::uvec3 size, glm::uvec3 group_size);
};

class DebugRegion {
  Device *m_device;
  VkCommandBuffer m_cmd_buffer;

  friend class CommandRecorder;
  DebugRegion(Device &device, VkCommandBuffer cmd_buffer, const char *label);

public:
  DebugRegion(const DebugRegion &) = delete;
  DebugRegion(DebugRegion &&) = delete;
  ~DebugRegion();

  DebugRegion &operator=(const DebugRegion &) = delete;
  DebugRegion &operator=(DebugRegion &&) = delete;
};

} // namespace ren
