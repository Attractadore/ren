#pragma once
#include "../core/Assert.hpp"
#include "../core/StdDef.hpp"
#include "Std.h"

namespace ren {

template <typename T> class DevicePtr {
public:
  DevicePtr() = default;

  DevicePtr(std::nullptr_t) : DevicePtr() {}

  explicit DevicePtr(u64 ptr) {
    if constexpr (not std::is_void_v<T>) {
      ren_assert_msg(ptr % alignof(T) == 0,
                     "Device pointer is improperly aligned");
    }
    m_ptr = ptr;
  }

  template <typename U>
  explicit DevicePtr(DevicePtr<U> other) : DevicePtr(u64(other)) {}

  bool is_null() const { return m_ptr == 0; }

  explicit operator bool() const { return !is_null(); }

  explicit operator u64() const { return m_ptr; }

  operator DevicePtr<void>() const { return DevicePtr<void>(m_ptr); }

  auto operator+=(i64 offset) -> DevicePtr &
    requires(not std::is_void_v<T>)
  {
    m_ptr += offset * sizeof(T);
    return *this;
  }

  auto operator+(i64 offset) const -> DevicePtr
    requires(not std::is_void_v<T>)
  {
    DevicePtr copy = *this;
    copy += offset;
    return copy;
  }

  auto operator-=(i64 offset) -> DevicePtr &
    requires(not std::is_void_v<T>)
  {
    return *this += -offset;
  }

  auto operator-(i64 offset) const -> DevicePtr
    requires(not std::is_void_v<T>)
  {
    return (*this) + (-offset);
  }

private:
  u64 m_ptr = 0;
};

#define GLSL_UNQUALIFIED_PTR(Type) ren::DevicePtr<Type>

#define GLSL_DEFINE_PTR_TYPE(Type, alignment)                                  \
  static_assert(alignof(Type) == alignment)

} // namespace ren

GLSL_NAMESPACE_BEGIN

static const uint DEFAULT_DEVICE_PTR_ALIGNMENT = 16;
static const uint DEVICE_CACHE_LINE_SIZE = 128;

GLSL_NAMESPACE_END
