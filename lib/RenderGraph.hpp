#pragma once
#include "BumpAllocator.hpp"
#include "CommandRecorder.hpp"
#include "DescriptorAllocator.hpp"
#include "Renderer.hpp"
#include "ResourceArena.hpp"
#include "Texture.hpp"
#include "core/DynamicBitset.hpp"
#include "core/Flags.hpp"
#include "core/GenArray.hpp"
#include "core/GenMap.hpp"
#include "core/NewType.hpp"
#include "core/String.hpp"
#include "core/Variant.hpp"
#include "ren/core/NotNull.hpp"

#include <functional>

namespace ren {

#if REN_RG_DEBUG
using RgDebugName = String;
#else
using RgDebugName = DummyString;
#endif

#define REN_RG_DEBUG_NAME_TYPE NO_UNIQUE_ADDRESS RgDebugName

class CommandRecorder;
class RenderPass;

class RgPersistent;
class RgBuilder;
class RgPassBuilder;
class RenderGraph;
class RgRuntime;

template <typename F>
concept CRgRenderPassCallback =
    std::invocable<F, Renderer &, const RgRuntime &, RenderPass &>;

using RgRenderPassCallback =
    std::function<void(Renderer &, const RgRuntime &, RenderPass &)>;
static_assert(CRgRenderPassCallback<RgRenderPassCallback>);

template <typename F>
concept CRgCallback =
    std::invocable<F, Renderer &, const RgRuntime &, CommandRecorder &>;

using RgCallback =
    std::function<void(Renderer &, const RgRuntime &, CommandRecorder &)>;
static_assert(CRgCallback<RgCallback>);

template <typename F>
concept CRgTextureInitCallback =
    std::invocable<F, Handle<Texture>, Renderer &, CommandRecorder &>;

using RgTextureInitCallback =
    std::function<void(Handle<Texture>, Renderer &, CommandRecorder &)>;
static_assert(CRgTextureInitCallback<RgTextureInitCallback>);

struct RgPass;
using RgPassId = Handle<RgPass>;

REN_NEW_TYPE(RgPhysicalBufferId, u32);

struct RgBuffer;
REN_NEW_TEMPLATE_TYPE(RgBufferId, Handle<RgBuffer>, T);
using RgUntypedBufferId = Handle<RgBuffer>;

template <typename T> class RgBufferToken {
public:
  RgBufferToken() = default;
  template <typename U>
    requires std::same_as<T, std::byte>
  RgBufferToken(RgBufferToken<U> other) {
    m_value = other;
  }

  explicit RgBufferToken(u32 value) { m_value = value; }

  operator u32() const { return m_value; }

  explicit operator bool() const { return *this != RgBufferToken(); }

private:
  u32 m_value = -1;
};
using RgUntypedBufferToken = RgBufferToken<std::byte>;

REN_NEW_TYPE(RgBufferUseId, u32);

REN_NEW_TYPE(RgPhysicalTextureId, u32);

struct RgTexture;
using RgTextureId = Handle<RgTexture>;

class RgTextureToken {
public:
  RgTextureToken() = default;

  explicit RgTextureToken(u32 value) { m_value = value; }

  operator u32() const { return m_value; }

  explicit operator bool() const { return *this != RgTextureToken(); }

private:
  u32 m_value = -1;
};

REN_NEW_TYPE(RgTextureUseId, u32);

struct RgSemaphore;
using RgSemaphoreId = Handle<RgSemaphore>;

REN_NEW_TYPE(RgSemaphoreStateId, u32);

REN_BEGIN_FLAGS_ENUM(RgQueue){
    REN_FLAG(Graphics),
    REN_FLAG(Async),
    None = 0,
} REN_END_FLAGS_ENUM(RgQueue);

} // namespace ren

REN_ENABLE_FLAGS(ren::RgQueue);

namespace ren {

using RgQueueMask = Flags<RgQueue>;

struct RgPassCreateInfo {
  REN_RG_DEBUG_NAME_TYPE name;
  RgQueue queue = RgQueue::Graphics;
};

struct RgRenderTarget {
  RgTextureToken texture;
  rhi::RenderTargetOperations ops;
};

struct RgDepthStencilTarget {
  RgTextureToken texture;
  rhi::DepthTargetOperations ops;
};

struct RgRenderPass {
  u32 num_render_targets = 0;
  RgRenderTarget render_targets[rhi::MAX_NUM_RENDER_TARGETS] = {};
  RgDepthStencilTarget depth_stencil_target;
  RgRenderPassCallback cb;
};

struct RgPass {
  RgQueue queue = {};
  bool signal = false;
  bool wait = false;
  u64 signal_time = 0;
  u64 wait_time = 0;
  SmallVector<RgBufferUseId> read_buffers;
  SmallVector<RgBufferUseId> write_buffers;
  SmallVector<RgTextureUseId> read_textures;
  SmallVector<RgTextureUseId> write_textures;
  SmallVector<RgSemaphoreStateId> wait_semaphores;
  SmallVector<RgSemaphoreStateId> signal_semaphores;
  Variant<Monostate, RgRenderPass, RgCallback> ext;
};

template <typename T> struct RgBufferCreateInfo {
  /// Buffer name.
  REN_RG_DEBUG_NAME_TYPE name;
  /// Memory heap from which to allocate buffer.
  rhi::MemoryHeap heap = rhi::MemoryHeap::Default;
  /// Buffer size.
  usize count = 1;
  /// Optional default value.
  Optional<T> init;
  /// Queue on which to perform fill with default value.
  RgQueue init_queue = RgQueue::Graphics;
};

struct RgPhysicalBuffer {
  RgQueueMask queues;
  rhi::MemoryHeap heap = {};
  usize size = 0;
  BufferView view;
};

struct RgBuffer {
#if REN_RG_DEBUG
  String name;
#endif
  RgPhysicalBufferId parent;
  RgPassId def;
  RgPassId kill;
#if REN_RG_DEBUG
  RgUntypedBufferId child;
#endif
};

struct RgBufferUse {
  RgUntypedBufferId buffer;
  u32 offset = 0;
  rhi::BufferState usage;
};

struct RgTextureCreateInfo {
  /// Texture name
  REN_RG_DEBUG_NAME_TYPE name;
  /// Texture format
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  /// Texture width
  u32 width = 1;
  /// Texture height
  u32 height = 1;
  /// Texture depth
  u32 depth : 31 = 1;
  bool cube_map : 1 = false;
  /// Number of mip levels
  u32 num_mips = 1;
  u32 num_layers = 1;
  bool persistent = false;
};

struct RgPhysicalTexture {
#if REN_RG_DEBUG
  String name;
#endif
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageUsageFlags usage = {};
  glm::uvec3 size = {};
  bool cube_map = false;
  u32 num_mips = 1;
  u32 num_layers = 1;
  Handle<Texture> handle;
  rhi::ImageLayout layout = rhi::ImageLayout::Undefined;
  RgTextureId id;
  RgQueue last_queue = RgQueue::None;
  RgQueue queue = RgQueue::None;
  u64 last_time = 0;
  u64 time = 0;
};

struct RgTexture {
#if REN_RG_DEBUG
  String name;
#endif
  RgPhysicalTextureId parent;
  RgPassId def;
  RgPassId kill;
  RgTextureId child;
};

struct RgTextureUse {
  RgTextureId texture;
  rhi::Sampler sampler;
  rhi::ImageState state;
  u32 base_mip = 0;
};

struct RgSemaphore {
#if REN_RG_DEBUG
  String name;
#endif
  Handle<Semaphore> handle;
};

namespace detail {

template <typename T> struct RgPushConstantImpl {
  using type = T;
};

template <typename T> struct RgPushConstantImpl<DevicePtr<T>> {
  using type = RgBufferToken<T>;
};

template <> struct RgPushConstantImpl<DevicePtr<void>> {
  using type = RgUntypedBufferToken;
};

template <typename T> struct IsDevicePtrPCImpl : std::false_type {};
template <typename T>
struct IsDevicePtrPCImpl<DevicePtr<T>> : std::true_type {};

template <typename T>
concept IsDevicePtr = IsDevicePtrPCImpl<T>::value;

template <typename T> struct RgPushConstantImpl<sh::Handle<T>> {
  using type = RgTextureToken;
};

template <sh::DescriptorOfKind<sh::DescriptorKind::RWTexture> T, usize N>
struct RgPushConstantImpl<std::array<sh::Handle<T>, N>> {
  using type = RgTextureToken;
};

template <typename T> struct RWTextureArrayImpl : std::false_type {};

template <sh::DescriptorOfKind<sh::DescriptorKind::RWTexture> T, usize N>
struct RWTextureArrayImpl<std::array<sh::Handle<T>, N>> : std::true_type {};

template <typename T>
concept RWTextureArray = RWTextureArrayImpl<T>::value;

}; // namespace detail

template <typename T>
using RgPushConstant = detail::RgPushConstantImpl<T>::type;

struct RgSemaphoreState {
  RgSemaphoreId semaphore;
  u64 value = 0;
};

struct RgBuildData {
  GenArray<RgPass> m_passes;
  Vector<RgPassId> m_gfx_schedule;
  Vector<RgPassId> m_async_schedule;

  Vector<RgPhysicalBuffer> m_physical_buffers;
  GenArray<RgBuffer> m_buffers;
  Vector<RgBufferUse> m_buffer_uses;

  Vector<RgTextureUse> m_texture_uses;

  Vector<RgSemaphoreState> m_semaphore_states;
};

struct RgRtRenderPass {
  u32 base_render_target = 0;
  u32 num_render_targets = 0;
  Optional<u32> depth_stencil_target;
  RgRenderPassCallback cb;
};

struct RgRtPass {
  RgPassId pass;
  u32 base_memory_barrier = 0;
  u32 num_memory_barriers = 0;
  u32 base_texture_barrier = 0;
  u32 num_texture_barriers = 0;
  u32 base_wait_semaphore = 0;
  u32 num_wait_semaphores = 0;
  u32 base_signal_semaphore = 0;
  u32 num_signal_semaphores = 0;
  Variant<RgRtRenderPass, RgCallback> ext;
};

struct RgTextureDescriptors {
  u32 num_mips = 0;
  sh::Handle<void> sampled;
  sh::Handle<void> combined;
  u32 *storage = nullptr;
};

struct RgRtData {
  Vector<RgRtPass> m_gfx_passes;
  Vector<RgRtPass> m_async_passes;
#if REN_RG_DEBUG
  GenMap<String, RgPassId> m_pass_names;
#endif

  Vector<RgRenderTarget> m_render_targets;
  Vector<RgDepthStencilTarget> m_depth_stencil_targets;

  Vector<BufferView> m_buffers;

  Vector<Handle<Texture>> m_textures;
  Vector<RgTextureDescriptors> m_texture_descriptors;
  Vector<u32> m_storage_texture_descriptors;

  Vector<rhi::MemoryBarrier> m_memory_barriers;
  Vector<TextureBarrier> m_texture_barriers;
  Vector<SemaphoreState> m_semaphore_submit_info;
};

class RgPersistent {
public:
  void init(Renderer *renderer) { m_arena.init(renderer); }

  [[nodiscard]] auto create_texture(RgTextureCreateInfo &&create_info)
      -> RgTextureId;

  [[nodiscard]] auto create_texture(RgDebugName name) -> RgTextureId;

  [[nodiscard]] auto create_semaphore(RgDebugName name) -> RgSemaphoreId;

  void set_async_compute_enabled(bool enabled);

  void reset();

private:
  friend class RgBuilder;
  friend class RenderGraph;

  void rotate_textures();

private:
  ResourceArena m_arena;
  Vector<RgPhysicalTexture> m_physical_textures;
  DynamicBitset m_persistent_textures;
  DynamicBitset m_external_textures;
  GenArray<RgTexture> m_textures;

  Vector<RgTextureId> m_frame_textures;

  GenArray<RgSemaphore> m_semaphores;

  bool m_async_compute = true;

  Handle<Semaphore> m_gfx_semaphore;
  Handle<Semaphore> m_async_semaphore;
  RgSemaphoreId m_gfx_semaphore_id = {};
  RgSemaphoreId m_async_semaphore_id = {};
  u64 m_gfx_time = 0;
  u64 m_async_time = 0;

  RgBuildData m_build_data;
  RgRtData m_rt_data;
};

struct RgBuildInfo {
  DeviceBumpAllocator *gfx_allocator = nullptr;
  DeviceBumpAllocator *async_allocator = nullptr;
  DeviceBumpAllocator *shared_allocator = nullptr;
  UploadBumpAllocator *upload_allocator = nullptr;
};

struct RgTextureWriteInfo {
  NO_UNIQUE_ADDRESS RgDebugName name;
  RgPassId pass;
  NotNull<RgTextureId *> texture;
  rhi::ImageState usage = rhi::CS_UNORDERED_ACCESS_IMAGE;
  rhi::Sampler sampler;
  u32 base_mip = 0;
};

class RgBuilder {
public:
  RgBuilder(RgPersistent &rgp, Renderer &renderer,
            DescriptorAllocatorScope &descriptor_allocator);

  [[nodiscard]] auto create_pass(RgPassCreateInfo &&create_info)
      -> RgPassBuilder;

  [[nodiscard]] auto create_buffer(RgDebugName name, rhi::MemoryHeap heap,
                                   usize size) -> RgUntypedBufferId;

  template <typename T>
  [[nodiscard]] auto create_buffer(RgBufferCreateInfo<T> &&create_info)
      -> RgBufferId<T>;

  template <typename T>
  [[nodiscard]] auto create_buffer(RgDebugName name,
                                   const BufferSlice<T> &slice)
      -> RgBufferId<T> {
    RgBufferId<T> buffer = create_buffer<T>({
        .name = std::move(name),
        .heap = m_renderer->get_buffer(slice.buffer).heap,
        .count = slice.count,
    });
    set_external_buffer(buffer, BufferView(slice));
    return buffer;
  }

  template <typename T>
  void fill_buffer(RgDebugName name, RgBufferId<T> *buffer, const T &value,
                   RgQueue queue = RgQueue::Graphics);

  template <typename T>
  void copy_buffer(RgBufferId<T> src, RgDebugName name, RgBufferId<T> *dst,
                   RgQueue queue = RgQueue::Graphics);

  template <typename T>
  void copy_buffer(RgBufferId<T> src, RgBufferId<T> *dst,
                   RgQueue queue = RgQueue::Graphics) {
    copy_buffer(src, "rg#", dst, queue);
  }

  void clear_texture(RgDebugName name, NotNull<RgTextureId *> texture,
                     const glm::vec4 &value, RgQueue queue = RgQueue::Graphics);

  void copy_texture_to_buffer(RgTextureId src, RgDebugName name,
                              RgUntypedBufferId *dst,
                              RgQueue queue = RgQueue::Graphics);

  void copy_texture_to_buffer(RgTextureId src, RgUntypedBufferId *dst,
                              RgQueue queue = RgQueue::Graphics) {
    copy_texture_to_buffer(src, "rg#", dst, queue);
  }

  void set_external_buffer(RgUntypedBufferId id, const BufferView &view);

  void set_external_texture(RgTextureId id, Handle<Texture> texture);

  void set_external_semaphore(RgSemaphoreId id, Handle<Semaphore> semaphore);

  auto build(const RgBuildInfo &build_info) -> Result<RenderGraph, Error>;

private:
  friend RgPassBuilder;

  [[nodiscard]] auto add_buffer_use(const RgBufferUse &use) -> RgBufferUseId;

  [[nodiscard]] auto create_virtual_buffer(RgPassId pass, RgDebugName name,
                                           RgUntypedBufferId parent)
      -> RgUntypedBufferId;

  [[nodiscard]] auto read_buffer(RgPassId pass, RgUntypedBufferId buffer,
                                 const rhi::BufferState &usage, u32 offset)
      -> RgUntypedBufferToken;

  [[nodiscard]] auto write_buffer(RgPassId pass, RgDebugName name,
                                  RgUntypedBufferId buffer,
                                  const rhi::BufferState &usage)
      -> std::tuple<RgUntypedBufferId, RgUntypedBufferToken>;

  [[nodiscard]] auto add_texture_use(const RgTextureUse &use) -> RgTextureUseId;

  [[nodiscard]] auto create_virtual_texture(RgPassId pass, RgDebugName name,
                                            RgTextureId parent) -> RgTextureId;

  [[nodiscard]] auto read_texture(RgPassId pass, RgTextureId texture,
                                  const rhi::ImageState &usage,
                                  rhi::Sampler sampler) -> RgTextureToken;

  [[nodiscard]] auto write_texture(RgTextureWriteInfo &&info) -> RgTextureToken;

  [[nodiscard]] auto add_semaphore_state(RgSemaphoreId semaphore, u64 value)
      -> RgSemaphoreStateId;

  void wait_semaphore(RgPassId pass, RgSemaphoreId semaphore, u64 value);

  void signal_semaphore(RgPassId pass, RgSemaphoreId semaphore, u64 value);

  void set_render_pass_callback(RgPassId id, CRgRenderPassCallback auto cb) {
    RgPass &pass = m_data->m_passes[id];
    ren_assert(pass.queue == RgQueue::Graphics);
    ren_assert(!pass.ext or pass.ext.get<RgRenderPass>());
    pass.ext.get_or_emplace<RgRenderPass>().cb = std::move(cb);
  }

  void set_callback(RgPassId id, CRgCallback auto cb) {
    RgPass &pass = m_data->m_passes[id];
    ren_assert(!pass.ext);
    pass.ext = RgCallback(std::move(cb));
  }

  auto alloc_textures() -> Result<void, Error>;

  void alloc_buffers(DeviceBumpAllocator &gfx_allocator,
                     DeviceBumpAllocator &async_allocator,
                     DeviceBumpAllocator &shared_allocator,
                     UploadBumpAllocator &upload_allocator);

  void add_inter_queue_semaphores();

  void dump_pass_schedule() const;

  void init_runtime_passes();

  void init_runtime_buffers();

  auto init_runtime_textures() -> Result<void, Error>;

  void place_barriers_and_semaphores();

private:
  Renderer *m_renderer = nullptr;
  RgPersistent *m_rgp = nullptr;
  RgBuildData *m_data = nullptr;
  RgRtData *m_rt_data = nullptr;
  DescriptorAllocatorScope *m_descriptor_allocator = nullptr;
};

struct RgExecuteInfo {
  Handle<CommandPool> gfx_cmd_pool;
  Handle<CommandPool> async_cmd_pool;
  EventPool *gfx_event_pool = nullptr;
  EventPool *async_event_pool = nullptr;
  Handle<Semaphore> *frame_end_semaphore = nullptr;
  u64 *frame_end_time = nullptr;
};

class RenderGraph {
public:
  auto execute(const RgExecuteInfo &execute_info) -> Result<void, Error>;

private:
  friend RgBuilder;
  friend RgRuntime;

private:
  Renderer *m_renderer = nullptr;
  RgPersistent *m_rgp = nullptr;
  RgRtData *m_data = nullptr;
  UploadBumpAllocator *m_upload_allocator = nullptr;
  EventPool *m_gfx_event_pool = nullptr;
  EventPool *m_async_event_pool = nullptr;
  const GenArray<RgSemaphore> *m_semaphores = nullptr;
};

class RgRuntime {
public:
  auto get_untyped_buffer(RgUntypedBufferToken buffer) const
      -> const BufferView &;

  template <typename T>
  auto get_buffer(RgBufferToken<T> buffer) const -> BufferSlice<T> {
    return BufferSlice<T>(get_untyped_buffer(RgUntypedBufferToken(buffer)));
  }

  template <typename T>
  auto get_buffer_device_ptr(RgBufferToken<T> buffer) const -> DevicePtr<T> {
    DevicePtr<T> ptr =
        m_rg->m_renderer->get_buffer_device_ptr(get_buffer(buffer));
    ren_assert(ptr);
    return ptr;
  }

  template <typename T>
  auto try_get_buffer_device_ptr(RgBufferToken<T> buffer) const
      -> DevicePtr<T> {
    if (!buffer) {
      return {};
    }
    DevicePtr<T> ptr =
        m_rg->m_renderer->get_buffer_device_ptr(get_buffer(buffer));
    ren_assert(ptr);
    return ptr;
  }

  template <typename T> auto map_buffer(RgBufferToken<T> buffer) const -> T * {
    return m_rg->m_renderer->map_buffer(get_buffer(buffer));
  }

  auto get_texture(RgTextureToken texture) const -> Handle<Texture>;

  auto get_texture_descriptor(RgTextureToken texture) const -> sh::Handle<void>;

  template <sh::DescriptorOfKind<sh::DescriptorKind::Texture> T>
  auto get_texture_descriptor(RgTextureToken texture) const -> sh::Handle<T> {
    return sh::Handle<T>(get_texture_descriptor(texture));
  }

  auto try_get_texture_descriptor(RgTextureToken texture) const
      -> sh::Handle<void>;

  template <sh::DescriptorOfKind<sh::DescriptorKind::Texture> T>
  auto try_get_texture_descriptor(RgTextureToken texture) const
      -> sh::Handle<T> {
    return sh::Handle<T>(try_get_texture_descriptor(texture));
  }

  auto get_sampled_texture_descriptor(RgTextureToken texture) const
      -> sh::Handle<void>;

  auto try_get_sampled_texture_descriptor(RgTextureToken texture) const
      -> sh::Handle<void>;

  auto get_storage_texture_descriptor(RgTextureToken texture, u32 mip = 0) const
      -> sh::Handle<void>;

  auto try_get_storage_texture_descriptor(RgTextureToken texture,
                                          u32 mip = 0) const
      -> sh::Handle<void>;

  template <sh::DescriptorOfKind<sh::DescriptorKind::RWTexture> T>
  auto get_storage_texture_descriptor(RgTextureToken texture, u32 mip = 0) const
      -> sh::Handle<T> {
    return sh::Handle<T>(get_storage_texture_descriptor(texture, mip));
  }

  template <sh::DescriptorOfKind<sh::DescriptorKind::RWTexture> T>
  auto try_get_storage_texture_descriptor(RgTextureToken texture,
                                          u32 mip = 0) const -> sh::Handle<T> {
    return sh::Handle<T>(try_get_storage_texture_descriptor(texture, mip));
  }

  auto get_allocator() const -> UploadBumpAllocator &;

  template <typename T = std::byte>
  auto allocate(usize count = 1) const -> UploadBumpAllocation<T> {
    return get_allocator().allocate<T>(count);
  }

  auto get_semaphore(RgSemaphoreId semaphore) const -> Handle<Semaphore>;

  template <typename T> static auto to_push_constant(const T &data) -> T {
    return data;
  }

  template <detail::IsDevicePtr P>
  auto to_push_constant(RgPushConstant<P> buffer) const -> P {
    return try_get_buffer_device_ptr(buffer);
  }

  auto to_push_constant(RgUntypedBufferToken buffer) const -> DevicePtr<void> {
    return try_get_buffer_device_ptr(buffer);
  }

  template <sh::HandleOfKind<sh::DescriptorKind::Texture> T>
  auto to_push_constant(RgTextureToken texture) const -> T {
    return T(try_get_texture_descriptor(texture));
  }

  template <sh::HandleOfKind<sh::DescriptorKind::Sampler> T>
  auto to_push_constant(RgTextureToken texture) const -> T {
    return T(try_get_sampled_texture_descriptor(texture));
  }

  template <sh::HandleOfKind<sh::DescriptorKind::RWTexture> T>
  auto to_push_constant(RgTextureToken texture) const -> T {
    return T(try_get_storage_texture_descriptor(texture));
  }

  template <detail::RWTextureArray A>
  auto to_push_constant(RgTextureToken texture) const -> A {
    A pc = {};
    for (usize i = 0; i < pc.size(); ++i) {
      pc[i] = typename A::value_type(
          try_get_storage_texture_descriptor(texture, i));
    }
    return pc;
  }

  template <typename PassArgs>
  void push_constants(RenderPass &render_pass, const PassArgs &args) const {
    auto pc = to_push_constants(*this, args);
    render_pass.push_constants(pc);
  }

  template <typename PassArgs>
  void push_constants(CommandRecorder &cmd, const PassArgs &args) const {
    auto pc = to_push_constants(*this, args);
    cmd.push_constants(pc);
  }

  auto get_event_pool() const -> EventPool & { return *m_event_pool; }

private:
  friend RenderGraph;

private:
  EventPool *m_event_pool = nullptr;
  RenderGraph *m_rg = nullptr;
};

class RgPassBuilder {
public:
  [[nodiscard]] auto read_buffer(RgUntypedBufferId buffer,
                                 const rhi::BufferState &usage, u32 offset)
      -> RgUntypedBufferToken;

  template <typename T>
  [[nodiscard]] auto
  read_buffer(RgBufferId<T> buffer,
              const rhi::BufferState &usage = rhi::CS_RESOURCE_BUFFER,
              u32 offset = 0) -> RgBufferToken<T> {
    return RgBufferToken<T>(
        read_buffer(RgUntypedBufferId(buffer), usage, offset * sizeof(T)));
  }

  template <typename T>
  [[nodiscard]] auto read_buffer(RgBufferId<T> buffer, u32 offset)
      -> RgBufferToken<T> {
    return read_buffer(buffer, rhi::CS_RESOURCE_BUFFER, offset);
  }

  [[nodiscard]] auto
  write_buffer(RgDebugName name, RgUntypedBufferId buffer,
               const rhi::BufferState &usage = rhi::CS_UNORDERED_ACCESS_BUFFER)
      -> std::tuple<RgUntypedBufferId, RgUntypedBufferToken>;

  [[nodiscard]] auto
  write_buffer(RgDebugName name, RgUntypedBufferId buffer,
               NotNull<RgUntypedBufferId *> new_buffer,
               const rhi::BufferState &usage = rhi::CS_UNORDERED_ACCESS_BUFFER)
      -> RgUntypedBufferToken {
    RgUntypedBufferToken token;
    std::tie(*new_buffer, token) = write_buffer(std::move(name), buffer, usage);
    return token;
  }

  [[nodiscard]] auto
  write_buffer(RgDebugName name, RgUntypedBufferId *buffer,
               const rhi::BufferState &usage = rhi::CS_UNORDERED_ACCESS_BUFFER)
      -> RgUntypedBufferToken {
    return write_buffer(std::move(name), *buffer, buffer, usage);
  }

  template <typename T>
  [[nodiscard]] auto
  write_buffer(RgDebugName name, RgBufferId<T> buffer,
               const rhi::BufferState &usage = rhi::CS_UNORDERED_ACCESS_BUFFER)
      -> std::tuple<RgBufferId<T>, RgBufferToken<T>> {
    auto [id, token] =
        write_buffer(std::move(name), RgUntypedBufferId(buffer), usage);
    return {RgBufferId<T>(id), RgBufferToken<T>(token)};
  }

  template <typename T>
  [[nodiscard]] auto
  write_buffer(RgDebugName name, RgBufferId<T> buffer,
               RgBufferId<T> *new_buffer,
               const rhi::BufferState &usage = rhi::CS_UNORDERED_ACCESS_BUFFER)
      -> RgBufferToken<T> {
    ren_assert(new_buffer);
    RgBufferToken<T> token;
    std::tie(*new_buffer, token) = write_buffer(std::move(name), buffer, usage);
    return token;
  }

  template <typename T>
  [[nodiscard]] auto
  write_buffer(RgDebugName name, RgBufferId<T> *buffer,
               const rhi::BufferState &usage = rhi::CS_UNORDERED_ACCESS_BUFFER)
      -> RgBufferToken<T> {
    ren_assert(buffer);
    return write_buffer(std::move(name), *buffer, buffer, usage);
  }

  [[nodiscard]] auto
  read_texture(RgTextureId texture,
               const rhi::ImageState &usage = rhi::CS_RESOURCE_IMAGE,
               rhi::Sampler sampler = {}) -> RgTextureToken {
    return m_builder->read_texture(m_pass, texture, usage, sampler);
  }

  [[nodiscard]] auto read_texture(RgTextureId texture, rhi::Sampler sampler)
      -> RgTextureToken {
    return read_texture(texture, rhi::CS_RESOURCE_IMAGE, sampler);
  }

  [[nodiscard]] auto read_texture(RgTextureId texture,
                                  const rhi::ImageState &usage,
                                  const rhi::SamplerCreateInfo &sampler_info)
      -> RgTextureToken {
    return read_texture(
        texture, usage,
        m_builder->m_renderer->get_sampler(sampler_info).value());
  }

  [[nodiscard]] auto read_texture(RgTextureId texture,
                                  const rhi::SamplerCreateInfo &sampler_info)
      -> RgTextureToken {
    return read_texture(texture, rhi::CS_RESOURCE_IMAGE, sampler_info);
  }

  [[nodiscard]] auto
  try_read_texture(RgTextureId texture,
                   const rhi::ImageState &usage = rhi::CS_RESOURCE_IMAGE,
                   rhi::Sampler sampler = {}) -> RgTextureToken {
    if (!texture) {
      return {};
    }
    return m_builder->read_texture(m_pass, texture, usage, sampler);
  }

  [[nodiscard]] auto
  try_read_texture(RgTextureId texture, const rhi::ImageState &usage,
                   const rhi::SamplerCreateInfo &sampler_info)
      -> RgTextureToken {
    return try_read_texture(
        texture, usage,
        m_builder->m_renderer->get_sampler(sampler_info).value());
  }

  [[nodiscard]] auto
  try_read_texture(RgTextureId texture,
                   const rhi::SamplerCreateInfo &sampler_info)
      -> RgTextureToken {
    return try_read_texture(texture, rhi::CS_RESOURCE_IMAGE, sampler_info);
  }

  auto
  write_texture(RgDebugName name, RgTextureId texture,
                const rhi::ImageState &usage = rhi::CS_UNORDERED_ACCESS_IMAGE)
      -> RgTextureToken;

  auto
  write_texture(RgDebugName name, NotNull<RgTextureId *> texture,
                const rhi::ImageState &usage = rhi::CS_UNORDERED_ACCESS_IMAGE)
      -> RgTextureToken;

  auto write_texture(RgDebugName name, NotNull<RgTextureId *> texture,
                     const rhi::ImageState &usage,
                     const rhi::SamplerCreateInfo &sampler, u32 base_mip = 0)
      -> RgTextureToken;

  auto write_render_target(RgDebugName name, NotNull<RgTextureId *> texture,
                           const rhi::RenderTargetOperations &ops,
                           u32 index = 0) -> RgTextureToken;

  auto read_depth_stencil_target(RgTextureId texture) -> RgTextureToken;

  auto write_depth_stencil_target(RgDebugName name,
                                  NotNull<RgTextureId *> texture,
                                  const rhi::DepthTargetOperations &ops)
      -> RgTextureToken;

  void wait_semaphore(RgSemaphoreId semaphore, u64 value = 0);

  void signal_semaphore(RgSemaphoreId semaphore, u64 value = 0);

  void set_render_pass_callback(CRgRenderPassCallback auto cb) {
    m_builder->set_render_pass_callback(m_pass, std::move(cb));
  }

  void set_callback(CRgCallback auto cb) {
    m_builder->set_callback(m_pass, std::move(cb));
  }

  void dispatch(Handle<ComputePipeline> pipeline, const auto &args,
                u32 num_groups_x, u32 num_groups_y = 1, u32 num_groups_z = 1) {
    set_callback([pipeline, args, num_groups_x, num_groups_y,
                  num_groups_z](Renderer &renderer, const RgRuntime &rg,
                                CommandRecorder &cmd) {
      cmd.bind_compute_pipeline(pipeline);
      rg.push_constants(cmd, args);
      cmd.dispatch(num_groups_x, num_groups_y, num_groups_z);
    });
  }

  void dispatch_grid(Handle<ComputePipeline> pipeline, const auto &args,
                     u32 size, u32 block_size_mult = 1) {
    dispatch_grid_3d(pipeline, args, {size, 1, 1}, {block_size_mult, 1, 1});
  }

  void dispatch_grid_2d(Handle<ComputePipeline> pipeline, const auto &args,
                        glm::uvec2 size, glm::uvec2 block_size_mult = {1, 1}) {
    dispatch_grid_3d(pipeline, args, {size, 1}, {block_size_mult, 1});
  }

  void dispatch_grid_3d(Handle<ComputePipeline> pipeline, const auto &args,
                        glm::uvec3 size,
                        glm::uvec3 block_size_mult = {1, 1, 1}) {
    set_callback([pipeline, args, size, block_size_mult](Renderer &renderer,
                                                         const RgRuntime &rg,
                                                         CommandRecorder &cmd) {
      cmd.bind_compute_pipeline(pipeline);
      rg.push_constants(cmd, args);
      cmd.dispatch_grid_3d(size, block_size_mult);
    });
  }

  void dispatch_indirect(Handle<ComputePipeline> pipeline, const auto &args,
                         RgBufferId<sh::DispatchIndirectCommand> command,
                         u32 offset = 0) {
    RgBufferToken<sh::DispatchIndirectCommand> token =
        read_buffer(command, rhi::INDIRECT_COMMAND_BUFFER, offset);
    set_callback([pipeline, args, token](Renderer &renderer,
                                         const RgRuntime &rg,
                                         CommandRecorder &cmd) {
      cmd.bind_compute_pipeline(pipeline);
      rg.push_constants(cmd, args);
      cmd.dispatch_indirect(rg.get_buffer(token));
    });
  }

private:
  friend class RgBuilder;

  RgPassBuilder(RgPassId pass, RgBuilder &builder);

  void add_render_target(u32 index, RgTextureToken texture,
                         const rhi::RenderTargetOperations &ops);

  void add_depth_stencil_target(RgTextureToken texture,
                                const rhi::DepthTargetOperations &ops);

private:
  RgPassId m_pass;
  RgBuilder *m_builder = nullptr;
};

template <typename T>
[[nodiscard]] auto RgBuilder::create_buffer(RgBufferCreateInfo<T> &&create_info)
    -> RgBufferId<T> {
  if (!create_info.init) {
    return RgBufferId<T>(create_buffer(std::move(create_info.name),
                                       create_info.heap,
                                       create_info.count * sizeof(T)));
  }
  RgBufferId<T> buffer(
      create_buffer("", create_info.heap, create_info.count * sizeof(T)));
  RgDebugName name = std::move(create_info.name);
#if REN_RG_DEBUG
  if (name.empty()) {
    name = "rg#";
  }
#endif
  fill_buffer(std::move(name), &buffer, *create_info.init,
              create_info.init_queue);
  return buffer;
}

template <typename T>
void RgBuilder::fill_buffer(RgDebugName name, RgBufferId<T> *buffer,
                            const T &value, RgQueue queue) {
  auto pass = create_pass({.name = "fill-buffer", .queue = queue});
  auto token =
      pass.write_buffer(std::move(name), buffer, rhi::TRANSFER_DST_BUFFER);
  pass.set_callback(
      [token, value](Renderer &, const RgRuntime &rg, CommandRecorder &cmd) {
        BufferSlice<T> buffer = rg.get_buffer(token);
        if constexpr (sizeof(T) == sizeof(u32)) {
          cmd.fill_buffer(buffer, value);
        } else {
          auto data = rg.allocate<T>(buffer.count);
          std::ranges::fill_n(data.host_ptr, buffer.count, value);
          cmd.copy_buffer(data.slice, buffer);
        }
      });
}

template <typename T>
void RgBuilder::copy_buffer(RgBufferId<T> src, RgDebugName name,
                            RgBufferId<T> *dst, RgQueue queue) {
  auto pass = create_pass({.name = "copy-buffer", .queue = queue});
  auto src_token = pass.read_buffer(src, rhi::TRANSFER_SRC_BUFFER);
  auto dst_token =
      pass.write_buffer(std::move(name), dst, rhi::TRANSFER_DST_BUFFER);
  pass.set_callback([src_token, dst_token](Renderer &, const RgRuntime &rg,
                                           CommandRecorder &cmd) {
    cmd.copy_buffer(rg.get_buffer(src_token), rg.get_buffer(dst_token));
  });
}

} // namespace ren
