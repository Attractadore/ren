#pragma once
#include "Buffer.hpp"
#include "Texture.hpp"
#include "core/NewType.hpp"
#include "rhi.hpp"
#include "sh/Std.h"

#include <bit>
#include <glm/glm.hpp>

namespace ren {

class Renderer;
class ResourceArena;
struct CommandPool;
struct ComputePipeline;
struct GraphicsPipeline;
struct Event;

struct RenderTarget {
  RtvDesc rtv;
  rhi::RenderTargetOperations ops;
};

struct DepthStencilTarget {
  RtvDesc dsv;
  rhi::DepthTargetOperations ops;
};

struct RenderPassInfo {
  TempSpan<const RenderTarget> render_targets;
  DepthStencilTarget depth_stencil_target;
};

class RenderPass;

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

  auto render_pass(const RenderPassInfo &&begin_info) -> RenderPass;

  void bind_compute_pipeline(Handle<ComputePipeline> pipeline);

  void push_constants(Span<const std::byte> data, unsigned offset = 0);

  template <typename T>
    requires(sizeof(T) <= rhi::MAX_PUSH_CONSTANTS_SIZE)
  void push_constants(const T &data, unsigned offset = 0) {
    push_constants(Span(&data, 1).as_bytes(), offset);
  }

  void dispatch(u32 num_groups_x, u32 num_groups_y = 1, u32 num_groups_z = 1);
  void dispatch(glm::uvec2 num_groups);
  void dispatch(glm::uvec3 num_groups);

  void dispatch_grid(u32 size, u32 group_size_mult = 1);
  void dispatch_grid_2d(glm::uvec2 size, glm::uvec2 group_size_mult = {1, 1});
  void dispatch_grid_3d(glm::uvec3 size,
                        glm::uvec3 group_size_mult = {1, 1, 1});

  void dispatch_indirect(const BufferSlice<sh::DispatchIndirectCommand> &slice);

  void copy_buffer(const BufferView &src, const BufferView &dst);

  template <typename T>
  void copy_buffer(const BufferSlice<T> &src, const BufferSlice<T> &dst) {
    copy_buffer(BufferView(src), BufferView(dst));
  }

  void copy_buffer_to_texture(const BufferView &src, Handle<Texture> dst,
                              u32 base_mip = 0, u32 num_mips = ALL_MIPS);

  void copy_texture_to_buffer(Handle<Texture> src, const BufferView &dst,
                              u32 base_mip = 0, u32 num_mips = ALL_MIPS);

  void fill_buffer(const BufferView &buffer, u32 value);

  template <typename T>
    requires(sizeof(T) == sizeof(u32))
  void fill_buffer(const BufferSlice<T> &buffer, T value) {
    fill_buffer(BufferView(buffer), std::bit_cast<u32>(value));
  }

  void clear_texture(Handle<Texture> texture, const glm::vec4 &color);

  void pipeline_barrier(TempSpan<const rhi::MemoryBarrier> memory_barriers,
                        TempSpan<const TextureBarrier> texture_barriers);

  void memory_barrier(const rhi::MemoryBarrier &barrier) {
    pipeline_barrier({&barrier, 1}, {});
  }

  void texture_barrier(const TextureBarrier &barrier) {
    pipeline_barrier({}, {&barrier, 1});
  }

  void set_event(Handle<Event> event,
                 TempSpan<const rhi::MemoryBarrier> memory_barriers,
                 TempSpan<const TextureBarrier> texture_barriers);

  void wait_event(Handle<Event> event,
                  TempSpan<const rhi::MemoryBarrier> memory_barriers,
                  TempSpan<const TextureBarrier> texture_barriers);

  void reset_event(Handle<Event> event,
                   rhi::PipelineStageMask stages = rhi::PipelineStage::All);

  [[nodiscard]] auto debug_region(String8 label) -> DebugRegion;

private:
  Renderer *m_renderer = nullptr;
  rhi::CommandBuffer m_cmd = {};
  Handle<ComputePipeline> m_pipeline;
};

class RenderPass {
public:
  RenderPass(const RenderPass &) = delete;
  RenderPass &operator=(const RenderPass &) = delete;
  RenderPass(RenderPass &&) = delete;
  RenderPass &operator=(RenderPass &&) = delete;
  ~RenderPass();

  void end();

  void set_viewports(TempSpan<const rhi::Viewport> viewports);

  void set_scissor_rects(TempSpan<const rhi::Rect2D> rects);

  void bind_graphics_pipeline(Handle<GraphicsPipeline> pipeline);

  void push_constants(Span<const std::byte> data, unsigned offset = 0);

  template <typename T>
    requires(sizeof(T) <= rhi::MAX_PUSH_CONSTANTS_SIZE)
  void push_constants(const T &data, unsigned offset = 0) {
    push_constants(Span(&data, 1).as_bytes(), offset);
  }

  void bind_index_buffer(const BufferView &view, rhi::IndexType index_type);

  void bind_index_buffer(const BufferSlice<u8> &slice) {
    bind_index_buffer(BufferView(slice), rhi::IndexType::UInt8);
  }

  void bind_index_buffer(const BufferSlice<u16> &slice) {
    bind_index_buffer(BufferView(slice), rhi::IndexType::UInt16);
  }

  void bind_index_buffer(const BufferSlice<u32> &slice) {
    bind_index_buffer(BufferView(slice), rhi::IndexType::UInt32);
  }

  void draw(const rhi::DrawInfo &draw_info);

  void draw_indexed(const rhi::DrawIndexedInfo &draw_info);

  void draw_indirect_count(const BufferSlice<sh::DrawIndirectCommand> &slice,
                           const BufferSlice<u32> &counter);

  void draw_indexed_indirect_count(
      const BufferSlice<sh::DrawIndexedIndirectCommand> &slice,
      const BufferSlice<u32> &counter);

private:
  friend class CommandRecorder;
  RenderPass(Renderer &renderer, rhi::CommandBuffer cmd,
             const RenderPassInfo &&begin_info);

private:
  Renderer *m_renderer = nullptr;
  rhi::CommandBuffer m_cmd = {};
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
  DebugRegion(rhi::CommandBuffer cmd_buffer, String8 label);

private:
  rhi::CommandBuffer m_cmd = {};
};

REN_NEW_TYPE(EventId, u32);

struct EventData {
  Handle<Event> handle;
  u32 memory_barrier_offset = 0;
  u32 memory_barrier_count = 0;
  u32 texture_barrier_offset = 0;
  u32 texture_barrier_count = 0;
};

#if 0

struct EventPool {
  ResourceArena *m_arena = nullptr;
  u32 m_index = 0;
};

auto init_event_pool(ResourceArena &arena) -> EventPool;

auto set_event(CommandRecorder &cmd, EventPool &pool,
               TempSpan<const rhi::MemoryBarrier> memory_barriers,
               TempSpan<const TextureBarrier> texture_barriers) -> EventId;

inline auto set_event(CommandRecorder &cmd, EventPool &pool,
                      const rhi::MemoryBarrier &barrier) -> EventId {
  return set_event(cmd, pool, {&barrier, 1}, {});
}

inline auto set_event(CommandRecorder &cmd, EventPool &pool,
                      const TextureBarrier &barrier) -> EventId {
  return set_event(cmd, pool, {}, {&barrier, 1});
}

void wait_event(CommandRecorder &cmd, const EventPool &pool, EventId event);

void reset_event_pool(CommandRecorder &cmd, EventPool &pool);

#endif

} // namespace ren
