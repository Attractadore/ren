#pragma once
#include "Buffer.hpp"
#include "Support/Math.hpp"
#include "Support/Optional.hpp"

#include <range/v3/algorithm.hpp>
#include <range/v3/view.hpp>

namespace ren {
class RingBufferAllocator {
  size_t m_position = 0;
  unsigned m_size;
  unsigned m_frame_idx = 0;
  std::array<size_t, 2> m_frame_starts;

  size_t physical_end() const {
    return m_position + m_size - m_position % m_size;
  }

  size_t logical_end() const {
    return m_frame_starts[(m_frame_idx + 1) % m_frame_starts.size()];
  }

public:
  explicit RingBufferAllocator(unsigned size) {
    m_size = size;
    m_frame_starts.fill(m_size);
  }

  unsigned size() const { return m_size; }

  void begin_frame() {
    m_frame_idx = (m_frame_idx + 1) % m_frame_starts.size();
    m_frame_starts[m_frame_idx] = m_position + m_size;
  }
  void end_frame() {}

  struct Allocation {
    unsigned offset;
    unsigned count;
  };

  // Write some of the data until the underlying buffer's end is reached or
  // there is no space left
  Allocation write(unsigned count, unsigned size, unsigned alignment) {
    assert(m_size % alignment == 0);
    auto position = [&] {
      // Shift to prevent false negative when m_position is 0
      constexpr auto ptr_shift = 1 << 31;
      auto *ptr = reinterpret_cast<void *>(m_position + ptr_shift);
      auto pend = physical_end();
      auto physical_space = pend - m_position;
      if (std::align(alignment, size, ptr, physical_space)) {
        return reinterpret_cast<size_t>(ptr) - ptr_shift;
      }
      return pend;
    }();
    if (position < logical_end()) {
      unsigned max_count = (logical_end() - position) / size;
      count = std::min(count, max_count);
      m_position = position + count * size;
      return {unsigned(position % m_size), count};
    } else {
      return {};
    }
  }
};

class RingBuffer {
  Buffer m_buffer;
  RingBufferAllocator m_rb;

public:
  explicit RingBuffer(Buffer buffer)
      : m_buffer(std::move(buffer)), m_rb(m_buffer.desc.size) {
    assert(m_buffer.desc.offset == 0);
  }

  unsigned size() const { return m_rb.size(); }

  void begin_frame() { m_rb.begin_frame(); }
  void end_frame() { m_rb.end_frame(); }

  template <ranges::sized_range R> auto write(R &&r) {
    using T = ranges::range_value_t<R>;
    return write_aligned(std::forward<R>(r), alignof(T));
  }

  template <ranges::sized_range R>
  auto write_aligned(R &&data, unsigned alignment) {
    using T = ranges::range_value_t<R>;
    auto alloc = m_rb.write(ranges::size(data), sizeof(T), alignment);
    ranges::copy(data | ranges::views::take(alloc.count),
                 m_buffer.map<T>(alloc.offset));
    return alloc;
  }

  template <ranges::sized_range R> auto write_unaligned(R &&r) {
    return write_aligned(std::forward<R>(r), 1);
  }

  const Buffer &get_buffer() const { return m_buffer; }
};
} // namespace ren
