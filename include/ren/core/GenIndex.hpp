#pragma once
#include "ren/core/StdDef.hpp"

namespace ren {

struct GenIndex {
  u32 gen : 8 = 0;
  u32 index : 24 = 0;

public:
  bool operator==(const GenIndex &) const = default;

  bool is_null() const { return index == 0; }

  explicit operator bool() const { return not is_null(); }

  operator u32() const { return index; }
};

template <typename T> class Handle : public GenIndex {};

struct NullHandleT {
  template <typename T> operator Handle<T>() const { return Handle<T>(); }
};

inline constexpr NullHandleT NullHandle;

} // namespace ren
