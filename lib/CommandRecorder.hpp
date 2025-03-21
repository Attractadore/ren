#pragma once
#include "Attachments.hpp"
#include "Buffer.hpp"
#include "Descriptors.hpp"
#include "Texture.hpp"
#include "core/Assert.hpp"
#include "core/Optional.hpp"
#include "core/Span.hpp"
#include "core/Vector.hpp"
#include "glsl/Indirect.h"

#include <glm/glm.hpp>

namespace ren {

class Renderer;
struct CommandPool;
struct ComputePipeline;
struct GraphicsPipeline;
struct PipelineLayout;

struct ColorAttachment {
  RtvDesc rtv;
  ColorAttachmentOperations ops;
};

struct DepthStencilAttachment {
  RtvDesc dsv;
  DepthAttachmentOperations ops;
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

class DebugRegion;

class CommandRecorder {
public:
  CommandRecorder() = default;
  CommandRecorder(const CommandRecorder &) = delete;
  CommandRecorder &operator=(const CommandRecorder &) = delete;
  CommandRecorder(CommandRecorder &&) = delete;
  CommandRecorder &operator=(CommandRecorder &&) = delete;

  auto begin(Renderer &renderer, Handle<CommandPool> cmd_pool)
      -> Result<void, Error>;

  auto end() -> Result<rhi::CommandBuffer, Error>;

  explicit operator bool() const { return !!m_cmd; }

  auto render_pass(const RenderPassBeginInfo &&begin_info) -> RenderPass;

  void bind_compute_pipeline(Handle<ComputePipeline> pipeline);

  void set_descriptor_heaps(Handle<ResourceDescriptorHeap> resource_heap,
                            Handle<SamplerDescriptorHeap> sampler_heap);

  void set_push_constants(Handle<PipelineLayout> layout,
                          TempSpan<const std::byte> data, unsigned offset = 0);

  void set_push_constants(TempSpan<const std::byte> data, unsigned offset = 0);

  template <typename T>
    requires(sizeof(T) <= rhi::MAX_PUSH_CONSTANTS_SIZE)
  void set_push_constants(Handle<PipelineLayout> layout, const T &data,
                          unsigned offset = 0) {
    set_push_constants(layout, TempSpan(&data, 1).as_bytes(), offset);
  }

  template <typename T>
    requires(sizeof(T) <= rhi::MAX_PUSH_CONSTANTS_SIZE)
  void set_push_constants(const T &data, unsigned offset = 0) {
    ren_assert_msg(m_pipeline_layout, "A compute pipeline must be bound");
    set_push_constants(m_pipeline_layout, TempSpan(&data, 1).as_bytes(),
                       offset);
  }

  void dispatch(u32 num_groups_x, u32 num_groups_y = 1, u32 num_groups_z = 1);
  void dispatch(glm::uvec2 num_groups);
  void dispatch(glm::uvec3 num_groups);

  void dispatch_grid(u32 size, u32 group_size_mult = 1);
  void dispatch_grid_2d(glm::uvec2 size, glm::uvec2 group_size_mult = {1, 1});
  void dispatch_grid_3d(glm::uvec3 size,
                        glm::uvec3 group_size_mult = {1, 1, 1});

  void dispatch_indirect(const BufferView &view);
  void
  dispatch_indirect(const BufferSlice<glsl::DispatchIndirectCommand> &slice);

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
  void fill_buffer(const BufferSlice<T> &buffer, T value) {
    fill_buffer(BufferView(buffer), std::bit_cast<u32>(value));
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

  void copy_texture_to_buffer(Handle<Texture> src, Handle<Buffer> dst,
                              TempSpan<const VkBufferImageCopy> regions);

  void copy_texture_to_buffer(Handle<Texture> src, const BufferView &dst,
                              u32 level = 0);

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

  void pipeline_barrier(TempSpan<const rhi::MemoryBarrier> memory_barriers,
                        TempSpan<const TextureBarrier> texture_barriers);

  void memory_barrier(const rhi::MemoryBarrier &barrier) {
    pipeline_barrier({&barrier, 1}, {});
  }

  void texture_barrier(const TextureBarrier &barrier) {
    pipeline_barrier({}, {&barrier, 1});
  }

  [[nodiscard]] auto debug_region(const char *label) -> DebugRegion;

private:
  Renderer *m_renderer = nullptr;
  rhi::CommandBuffer m_cmd = {};
  Handle<ComputePipeline> m_pipeline;
  Handle<PipelineLayout> m_pipeline_layout;
};

class RenderPass {
public:
  RenderPass(const RenderPass &) = delete;
  RenderPass &operator=(const RenderPass &) = delete;
  RenderPass(RenderPass &&) = delete;
  RenderPass &operator=(RenderPass &&) = delete;
  ~RenderPass();

  void end();

  void set_viewports(
      StaticVector<VkViewport, rhi::MAX_NUM_RENDER_TARGETS> viewports);

  void set_scissor_rects(TempSpan<const VkRect2D> rects);

  void set_depth_compare_op(VkCompareOp op);

  void bind_graphics_pipeline(Handle<GraphicsPipeline> pipeline);

  void set_descriptor_heaps(Handle<ResourceDescriptorHeap> resource_heap,
                            Handle<SamplerDescriptorHeap> sampler_heap);

  void set_push_constants(Handle<PipelineLayout> layout,
                          TempSpan<const std::byte> data, unsigned offset = 0);

  void set_push_constants(TempSpan<const std::byte> data, unsigned offset = 0);

  template <typename T>
    requires(sizeof(T) <= rhi::MAX_PUSH_CONSTANTS_SIZE)
  void set_push_constants(Handle<PipelineLayout> layout, const T &data,
                          unsigned offset = 0) {
    set_push_constants(layout, TempSpan(&data, 1).as_bytes(), offset);
  }

  template <typename T>
    requires(sizeof(T) <= rhi::MAX_PUSH_CONSTANTS_SIZE)
  void set_push_constants(const T &data, unsigned offset = 0) {
    ren_assert_msg(m_pipeline_layout, "A graphics pipeline must be bound");
    set_push_constants(m_pipeline_layout, TempSpan(&data, 1).as_bytes(),
                       offset);
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

private:
  friend class CommandRecorder;
  RenderPass(Renderer &renderer, rhi::CommandBuffer cmd,
             const RenderPassBeginInfo &&begin_info);

private:
  Renderer *m_renderer = nullptr;
  rhi::CommandBuffer m_cmd = {};
  Handle<PipelineLayout> m_pipeline_layout;
};

class DebugRegion {
public:
  DebugRegion(const DebugRegion &) = delete;
  DebugRegion &operator=(const DebugRegion &) = delete;
  DebugRegion(DebugRegion &&) = delete;
  DebugRegion &operator=(DebugRegion &&) = delete;
  ~DebugRegion();

  void end();

private:
  friend class CommandRecorder;
  DebugRegion(rhi::CommandBuffer cmd_buffer, const char *label);

private:
  rhi::CommandBuffer m_cmd = {};
};

} // namespace ren
