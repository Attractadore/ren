#pragma once
#include <cstddef>
#include <cstdint>

namespace ren {

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using isize = ptrdiff_t;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using usize = size_t;

constexpr usize KiB = 1024;
constexpr usize MiB = 1024 * KiB;
constexpr usize GiB = 1024 * MiB;

template <typename I> struct Range {
  I b = 0;
  I e = 0;

public:
  struct Iterator {
    I m_value = 0;

  public:
    I operator*() { return m_value; }
    I operator++() { return ++m_value; }
    I operator++(int) { return m_value++; }

    bool operator==(const Iterator &) const = default;
  };

  Iterator begin() const { return {b}; }
  Iterator end() const { return {e}; }
};

template <typename I> auto range(I begin, I end) { return Range(begin, end); }

template <typename I> auto range(I end) { return Range(I(0), end); }

template <typename T, usize N> constexpr usize size(T (&)[N]) { return N; }

} // namespace ren

#if _MSC_VER
#define NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

#define ren_cat_impl(a, b) a##b
#define ren_cat(a, b) ren_cat_impl(a, b)

#if __GNUC__
#define ren_trap __builtin_trap
#elif _MSC_VER
#define ren_trap __debugbreak
#else
#include <cstdlib>
#define ren_trap std::abort
#endif

#define container_of(ptr, type, member)                                        \
  ((type *)((ren::u8 *)ptr - offsetof(type, member)))

#if __GNUC__
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define NOINLINE __attribute__((noinline))
#elif __clang__
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define NOINLINE __attribute__((noinline))
#elif _MSC_VER
#define ALWAYS_INLINE __forceinline inline
#define NOINLINE __declspec(noinline)
#endif
