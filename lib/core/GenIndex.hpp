#pragma once
#include "Hash.hpp"
#include "StdDef.hpp"

namespace ren {

namespace detail {

inline constexpr u8 TOMBSTONE = 0;
inline constexpr u8 INIT = TOMBSTONE + 1;

inline constexpr bool is_active(u8 gen) { return gen % 2 != TOMBSTONE % 2; }

}; // namespace detail

class GenIndex {
public:
  GenIndex() = default;

  bool operator==(const GenIndex &) const = default;

  bool is_null() const { return *this == GenIndex(); }

  explicit operator bool() const { return not is_null(); }

  operator u32() const { return index; }

protected:
  template <std::derived_from<GenIndex> K> friend class GenIndexPool;
  template <typename T, std::derived_from<GenIndex> K> friend class GenMap;

  static constexpr bool is_active(u8 gen) { return detail::is_active(gen); }

protected:
  static constexpr u8 TOMBSTONE = detail::TOMBSTONE;
  static constexpr u8 INIT = detail::INIT;

  u32 gen : 8 = TOMBSTONE;
  u32 index : 24 = 0;
};

template <> struct Hash<GenIndex> {
  auto operator()(GenIndex gi) const -> u64 {
    return Hash<u32>()(std::bit_cast<u32>(gi));
  };
};

template <typename T> class Handle : public GenIndex {};

template <typename T> struct Hash<Handle<T>> {
  auto operator()(Handle<T> handle) const -> u64 {
    return Hash<GenIndex>()(handle);
  };
};

struct NullHandleT {
  template <typename T> operator Handle<T>() const { return Handle<T>(); }
};

inline constexpr NullHandleT NullHandle;

} // namespace ren
