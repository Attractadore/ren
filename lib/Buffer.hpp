#pragma once
#include "DebugNames.hpp"
#include "Support/Assert.hpp"
#include "Support/GenIndex.hpp"
#include "Support/Hash.hpp"
#include "Support/StdDef.hpp"

#include <vk_mem_alloc.h>

namespace ren {

enum class BufferHeap {
  Static,
  Dynamic,
  Staging,
  Readback,
};
constexpr usize NUM_BUFFER_HEAPS = 4;

struct BufferCreateInfo {
  REN_DEBUG_NAME_FIELD("Buffer");
  BufferHeap heap = BufferHeap::Static;
  VkBufferUsageFlags usage = 0;
  union {
    usize size = 0;
    usize count;
  };
};

struct Buffer {
  VkBuffer handle;
  VmaAllocation allocation;
  std::byte *ptr;
  u64 address;
  usize size;
  BufferHeap heap;
  VkBufferUsageFlags usage;
};

struct BufferState {
  /// Pipeline stages in which this buffer is accessed
  VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
  /// Memory accesses performed on this buffer
  VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
};

using MemoryState = BufferState;

constexpr auto operator|(const BufferState &lhs,
                         const BufferState &rhs) -> BufferState {
  return {
      .stage_mask = lhs.stage_mask | rhs.stage_mask,
      .access_mask = lhs.access_mask | rhs.access_mask,
  };
};

constexpr BufferState HOST_WRITE_BUFFER = {};

constexpr BufferState VS_READ_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
};

constexpr BufferState FS_READ_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
};

constexpr BufferState TRANSFER_SRC_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
};

constexpr BufferState TRANSFER_DST_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
};

constexpr BufferState CS_READ_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
};

constexpr BufferState CS_WRITE_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
};

constexpr BufferState CS_READ_WRITE_BUFFER = CS_READ_BUFFER | CS_WRITE_BUFFER;

constexpr BufferState INDEX_SRC_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
    .access_mask = VK_ACCESS_2_INDEX_READ_BIT,
};

constexpr BufferState INDIRECT_COMMAND_SRC_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
    .access_mask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
};

template <typename T> struct BufferSlice {
  Handle<Buffer> buffer;
  usize offset = 0;
  usize count = 0;

public:
  bool operator==(const BufferSlice &) const = default;

  auto slice(usize start, usize new_count) const -> BufferSlice {
    ren_assert(start <= count);
    ren_assert(start + new_count <= count);
    return {
        .buffer = buffer,
        .offset = offset + start * sizeof(T),
        .count = new_count,
    };
  }

  auto slice(usize start) const -> BufferSlice {
    ren_assert(start <= count);
    return slice(start, count - start);
  }

  auto size_bytes() const -> usize { return count * sizeof(T); }

  template <typename U> explicit operator BufferSlice<U>() const {
    ren_assert(offset % alignof(U) == 0);
    ren_assert(size_bytes() % sizeof(U) == 0);
    return {
        .buffer = buffer,
        .offset = offset,
        .count = size_bytes() / sizeof(U),
    };
  }
};

using BufferView = BufferSlice<std::byte>;

REN_DEFINE_TYPE_HASH(BufferView, buffer, offset, count);

template <typename T> struct StatefulBufferSlice {
  BufferSlice<T> slice;
  BufferState state;
};

} // namespace ren
