#pragma once
#include "../Support/Errors.hpp"
#include "../Support/StdDef.hpp"

namespace ren {

template <typename T> class DevicePtr {
public:
  DevicePtr() = default;
  DevicePtr(std::nullptr_t) : DevicePtr() {}
  explicit DevicePtr(u64 ptr) {
    ren_assert_msg(ptr % alignof(T) == 0,
                   "Device pointer is improperly aligned");
    m_ptr = ptr;
  }

  bool is_null() const { return m_ptr == 0; }

  explicit operator bool() const { return !is_null(); }

  auto operator+=(isize offset) -> DevicePtr & {
    m_ptr += offset * sizeof(T);
    return *this;
  }

  auto operator+(isize offset) const -> DevicePtr {
    DevicePtr copy = *this;
    copy += offset;
    return copy;
  }

  auto operator-=(isize offset) -> DevicePtr & { return *this += -offset; }

  auto operator-(isize offset) const -> DevicePtr {
    return (*this) + (-offset);
  }

private:
  u64 m_ptr = 0;
};

#define GLSL_PTR(Type) ren::DevicePtr<Type>

#define GLSL_DEFINE_PTR_TYPE(Type, alignment)                                  \
  static_assert(alignof(Type) == alignment)

#define GLSL_IS_NULL(ptr) (ptr.is_null())

} // namespace ren
