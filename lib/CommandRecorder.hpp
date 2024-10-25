#pragma once
#include "Attachments.hpp"
#include "Buffer.hpp"
#include "Config.hpp"
#include "Support/Assert.hpp"
#include "Support/Optional.hpp"
#include "Support/Span.hpp"
#include "Support/Vector.hpp"
#include "Texture.hpp"
#include "glsl/Indirect.h"

#include <glm/glm.hpp>

namespace ren {

class Renderer;

struct ComputePipeline;
struct GraphicsPipeline;
struct PipelineLayout;

auto get_num_dispatch_groups(u32 size, u32 group_size) -> u32;

auto get_num_dispatch_groups(glm::uvec2 size, glm::uvec2 group_size)
    -> glm::uvec2;

auto get_num_dispatch_groups(glm::uvec3 size, glm::uvec3 group_size)
    -> glm::uvec3;

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

struct DrawInfo {
  u32 num_vertices = 0;
  u32 num_instances = 1;
  u32 first_vertex = 0;
  u32 first_instance = 0;
};

struct DrawIndexedInfo {
  u32 num_indices = 0;
  u32 num_instances = 1;
  u32 first_index = 0;
  i32 vertex_offset = 0;
  u32 first_instance = 0;
};

struct DrawIndirectCountInfo {
  BufferView buffer;
  BufferView count_buffer;
  u32 max_count;
};

class ComputePass;

class DebugRegion;

class CommandRecorder {
  Renderer *m_renderer = nullptr;
  VkCommandBuffer m_cmd_buffer = nullptr;

public:
  CommandRecorder(Renderer &renderer, VkCommandBuffer cmd_buffer);
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

  template <typename T>
  void copy_buffer(const BufferSlice<T> &src, const BufferSlice<T> &dst) {
    copy_buffer(BufferView(src), BufferView(dst));
  }

  void copy_buffer_to_texture(Handle<Buffer> src, Handle<Texture> dst,
                              TempSpan<const VkBufferImageCopy> regions);

  void copy_buffer_to_texture(const BufferView &src, Handle<Texture> dst,
                              u32 level = 0);

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

  void update_buffer(const BufferView &buffer, TempSpan<const std::byte> data);

  template <typename T>
    requires(sizeof(T) % 4 == 0)
  void update_buffer(const BufferView &buffer, const T &data) {
    update_buffer<T>(buffer, TempSpan(&data, 1));
  }

  void blit(Handle<Texture> src, Handle<Texture> dst,
            TempSpan<const VkImageBlit> regions, VkFilter filter);

  void clear_texture(Handle<Texture> texture,
                     TempSpan<const VkClearColorValue> clear_colors,
                     TempSpan<const VkImageSubresourceRange> clear_ranges);

  void clear_texture(Handle<Texture> texture,
                     const VkClearColorValue &clear_color);

  void clear_texture(Handle<Texture> texture, const glm::vec4 &clear_color);

  void
  clear_texture(Handle<Texture> texture,
                TempSpan<const VkClearDepthStencilValue> clear_depth_stencils,
                TempSpan<const VkImageSubresourceRange> clear_ranges);

  void clear_texture(Handle<Texture> texture,
                     const VkClearDepthStencilValue &clear_depth_stencil);

  void copy_texture(Handle<Texture> src, Handle<Texture> dst);

  void pipeline_barrier(const VkDependencyInfo &dependency_info);

  void pipeline_barrier(TempSpan<const VkMemoryBarrier2> barriers,
                        TempSpan<const VkImageMemoryBarrier2> image_barriers);

  [[nodiscard]] auto debug_region(const char *label) -> DebugRegion;
};

class RenderPass {
  Renderer *m_renderer = nullptr;
  VkCommandBuffer m_cmd_buffer = nullptr;
  Handle<PipelineLayout> m_pipeline_layout;
  VkShaderStageFlags m_shader_stages = 0;

  friend class CommandRecorder;
  RenderPass(Renderer &renderer, VkCommandBuffer cmd_buffer,
             const RenderPassBeginInfo &&begin_info);

public:
  RenderPass(const RenderPass &) = delete;
  RenderPass(RenderPass &&) = delete;
  ~RenderPass();

  RenderPass &operator=(const RenderPass &) = delete;
  RenderPass &operator=(RenderPass &&) = delete;

  void set_viewports(StaticVector<VkViewport, MAX_COLOR_ATTACHMENTS> viewports);

  void set_scissor_rects(TempSpan<const VkRect2D> rects);

  void set_depth_compare_op(VkCompareOp op);

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

  template <typename T>
    requires(sizeof(T) <= MAX_PUSH_CONSTANTS_SIZE)
  void set_push_constants(const T &data, unsigned offset = 0) {
    ren_assert_msg(m_pipeline_layout, "A graphics pipeline must be bound");
    set_push_constants(m_pipeline_layout, m_shader_stages,
                       TempSpan(&data, 1).as_bytes(), offset);
  }

  void bind_index_buffer(Handle<Buffer> buffer, VkIndexType type,
                         u32 offset = 0);
  void bind_index_buffer(const BufferView &view, VkIndexType type);
  void bind_index_buffer(const BufferSlice<u8> &slice);
  void bind_index_buffer(const BufferSlice<u16> &slice);
  void bind_index_buffer(const BufferSlice<u32> &slice);

  void draw(const DrawInfo &&draw_info);

  void draw_indexed(const DrawIndexedInfo &&draw_info);

  void draw_indirect(const BufferView &view,
                     usize stride = sizeof(VkDrawIndirectCommand));

  void draw_indirect_count(const BufferView &view, const BufferView &counter,
                           usize stride = sizeof(VkDrawIndirectCommand));

  void
  draw_indexed_indirect(const BufferView &view,
                        usize stride = sizeof(VkDrawIndexedIndirectCommand));

  void draw_indexed_indirect_count(
      const BufferView &view, const BufferView &counter,
      usize stride = sizeof(VkDrawIndexedIndirectCommand));

  void draw_indexed_indirect_count(
      const BufferSlice<glsl::DrawIndexedIndirectCommand> &commands,
      const BufferSlice<u32> &counter);
};

class ComputePass {
  Renderer *m_renderer = nullptr;
  VkCommandBuffer m_cmd_buffer = nullptr;
  Handle<PipelineLayout> m_pipeline_layout;

  friend class CommandRecorder;
  ComputePass(Renderer &renderer, VkCommandBuffer cmd_buffer);

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

  template <typename T>
    requires(sizeof(T) <= MAX_PUSH_CONSTANTS_SIZE)
  void set_push_constants(const T &data, unsigned offset = 0) {
    ren_assert_msg(m_pipeline_layout, "A compute pipeline must be bound");
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

  void dispatch_indirect(const BufferView &view);
  void
  dispatch_indirect(const BufferSlice<glsl::DispatchIndirectCommand> &slice);
};

class DebugRegion {
  VkCommandBuffer m_cmd_buffer = nullptr;

  friend class CommandRecorder;
  DebugRegion(VkCommandBuffer cmd_buffer, const char *label);

public:
  DebugRegion(const DebugRegion &) = delete;
  DebugRegion(DebugRegion &&);
  ~DebugRegion();

  DebugRegion &operator=(const DebugRegion &) = delete;
  DebugRegion &operator=(DebugRegion &&) = delete;
};

} // namespace ren
