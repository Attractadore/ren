#pragma once
#include "../Support/Errors.hpp"
#include "../Support/StdDef.hpp"

namespace ren {

template <typename T> class BufferReference {
  u64 m_value = 0;

public:
  BufferReference() = default;
  BufferReference(std::nullptr_t) : BufferReference() {}
  explicit BufferReference(u64 value) {
    ren_assert_msg(value % alignof(T) == 0,
                   "Buffer reference improperly aligned");
    m_value = value;
  }

  bool is_null() const { return m_value == 0; }

  explicit operator bool() const { return !is_null(); }
};

#define GLSL_REF_TYPE(alignment) struct alignas(alignment)

#define GLSL_REF(RefType) ren::BufferReference<RefType>

#define GLSL_IS_NULL(ref) (ref.is_null())

#define GLSL_SIZEOF(RefType) (sizeof(RefType))

} // namespace ren
