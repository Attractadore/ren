#pragma once
#include "core/GenIndex.hpp"
#include "ren/core/Assert.hpp"
#include "ren/core/StdDef.hpp"
#include "ren/core/String.hpp"
#include "rhi.hpp"

namespace ren {

struct BufferCreateInfo {
  String8 name = "Buffer";
  rhi::MemoryHeap heap = rhi::MemoryHeap::Default;
  union {
    usize size = 0;
    usize count;
  };
};

struct Buffer {
  rhi::Buffer handle = {};
  std::byte *ptr = nullptr;
  u64 address = 0;
  usize size = 0;
  rhi::MemoryHeap heap = rhi::MemoryHeap::Default;
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

} // namespace ren
