#pragma once
#include "Support/Errors.hpp"
#include "Support/StdDef.hpp"

namespace ren {

template <typename T> class BufferReference {
  u64 m_value = 0;

public:
  BufferReference() = default;
  BufferReference(std::nullptr_t) : BufferReference() {}
  explicit BufferReference(u64 value) {
    ren_assert(value % alignof(T) == 0, "Buffer reference improperly aligned");
    m_value = value;
  }
};

constexpr usize DEFAULT_BUFFER_REFERENCE_ALIGNMENT = 16;

} // namespace ren
