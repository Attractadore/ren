#pragma once
#include "../Support/Errors.hpp"
#include "../Support/StdDef.hpp"

namespace ren {

template <typename T> class DevicePtr {
  u64 m_value = 0;

public:
  DevicePtr() = default;
  DevicePtr(std::nullptr_t) : DevicePtr() {}
  explicit DevicePtr(u64 value) {
    ren_assert_msg(value % alignof(T) == 0,
                   "Device pointer improperly aligned");
    m_value = value;
  }

  bool is_null() const { return m_value == 0; }

  explicit operator bool() const { return !is_null(); }
};

#define GLSL_PTR(Type) ren::DevicePtr<Type>

#define GLSL_DEFINE_PTR_TYPE(Type, alignment)                                  \
  static_assert(alignof(Type) == alignment)

#define GLSL_IS_NULL(ptr) (ptr.is_null())

} // namespace ren
