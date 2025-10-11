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
