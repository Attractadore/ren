#pragma once
#include "Flags.hpp"

#include <boost/preprocessor/seq.hpp>
#include <boost/preprocessor/stringize.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace ren {
#define REN_DEFINE_ENUM(E, fields) enum class E { BOOST_PP_SEQ_ENUM(fields) }

#define REN_DEFINE_C_ENUM(E, fields) enum E { BOOST_PP_SEQ_ENUM(fields) }

#define REN_DEFINE_ENUM_WITH_UNKNOWN(E, fields)                                \
  enum class E { Unknown = 0, Undefined = 0, BOOST_PP_SEQ_ENUM(fields) }

namespace detail {
template <size_t N>
using FlagsSizeT = decltype([] {
  if constexpr (N <= 32) {
    return uint32_t();
  } else if constexpr (N <= 64) {
    return uint64_t();
  }
}());
}

#define REN_DETAIL_DEFINE_FLAGS_ENUM_FIELD(r, data, i, field) field = 1 << i,

#define REN_DEFINE_FLAGS_ENUM(E, fields)                                       \
  enum class E : detail::FlagsSizeT<BOOST_PP_SEQ_SIZE(fields)>{                \
      BOOST_PP_SEQ_FOR_EACH_I(REN_DETAIL_DEFINE_FLAGS_ENUM_FIELD, ~, fields)}; \
  ENABLE_FLAGS(E)

#define REN_DEFINE_FLAGS_ENUM_WITH_UNKNOWN(E, fields)                          \
  enum class E : detail::FlagsSizeT<BOOST_PP_SEQ_SIZE(fields)>{                \
      Unknown = 0, Undefined = 0,                                              \
      BOOST_PP_SEQ_FOR_EACH_I(REN_DETAIL_DEFINE_FLAGS_ENUM_FIELD, ~, fields)}; \
  ENABLE_FLAGS(E)

namespace detail {
template <typename E> struct EnumConvert;
template <typename E> using EnumConvertT = typename EnumConvert<E>::type;

template <auto From> constexpr bool FieldIsMapped = false;
template <auto From>
  requires FieldIsMapped<From>
constexpr EnumConvertT<decltype(From)> EnumFieldMap;

template <typename E> struct EnumFlags {
  using type = E;
};
template <FlagsEnum E> struct EnumFlags<E> {
  using type = Flags<E>;
};
template <typename E> using EnumFlagsT = typename EnumFlags<E>::type;
template <typename E>
using EnumConvertFlagsT = typename EnumFlags<EnumConvertT<E>>::type;
} // namespace detail

#define REN_MAP_TYPE(From, To)                                                 \
  template <> struct detail::EnumConvert<From> {                               \
    using type = To;                                                           \
  };

#define REN_MAP_FIELD(from, to)                                                \
  namespace detail {                                                           \
  template <> inline constexpr bool FieldIsMapped<from> = true;                \
  template <> inline constexpr auto EnumFieldMap<from> = to;                   \
  }

#define REN_ENUM_FLAGS(E, F)                                                   \
  template <> struct detail::EnumFlags<E> {                                    \
    using type = F;                                                            \
  };

#define REN_DETAIL_DEFINE_FROM_CASE(r, data, e)                                \
  case e:                                                                      \
    return detail::EnumFieldMap<e>;

#define REN_MAP_ENUM(name, E, fields)                                          \
  inline detail::EnumConvertT<E> name(E from) {                                \
    using enum E;                                                              \
    switch (from) {                                                            \
      BOOST_PP_SEQ_FOR_EACH(REN_DETAIL_DEFINE_FROM_CASE, ~, fields)            \
    }                                                                          \
    /* Silence GCC -Wreturn-type */                                            \
    assert(!"Unhandled enum value");                                           \
  }

namespace detail {
template <typename E>
concept EnumWithUnknown = requires { E::Unknown; };
} // namespace detail

#define REN_DETAIL_DEFINE_TO_CASE(r, data, e)                                  \
  case detail::EnumFieldMap<e>:                                                \
    return e;

#define REN_REVERSE_MAP_ENUM(name, E, fields)                                  \
  inline E name(detail::EnumConvertT<E> from) {                                \
    using enum E;                                                              \
    switch (from) {                                                            \
    default: {                                                                 \
      if constexpr (detail::EnumWithUnknown<E>) {                              \
        return static_cast<E>(0);                                              \
      } else {                                                                 \
        assert(!"Unhandled enum value");                                       \
      }                                                                        \
    }                                                                          \
      BOOST_PP_SEQ_FOR_EACH(REN_DETAIL_DEFINE_TO_CASE, ~, fields)              \
    }                                                                          \
  }

namespace detail {
template <typename E, auto convert>
EnumConvertFlagsT<E> mapFlags(EnumFlagsT<E> from_flags) {
  EnumConvertFlagsT<E> to_flags{};
  auto flags = static_cast<typename Flags<E>::Underlying>(from_flags.get());
  while (flags) {
    auto lsb_mask = flags & (~flags + 1);
    auto flag = flags & lsb_mask;
    to_flags |= convert(static_cast<E>(flag));
    flags = flags & ~lsb_mask;
  }
  return to_flags;
}

template <typename E, auto convert>
EnumFlagsT<E> reverseMapFlags(EnumConvertFlagsT<E> flags) {
  EnumFlagsT<E> to_flags{};
  while (flags) {
    auto lsb_mask = flags & (~flags + 1);
    auto flag = flags & lsb_mask;
    to_flags |= convert(static_cast<EnumConvertT<E>>(flag));
    flags = static_cast<EnumConvertFlagsT<E>>(flags & ~lsb_mask);
  }
  return to_flags;
}
} // namespace detail

#define REN_MAP_ENUM_AND_FLAGS(name, E, fields)                                \
  REN_MAP_ENUM(name, E, fields)                                                \
  constexpr auto name##Flags = detail::mapFlags<E, name>

#define REN_REVERSE_MAP_ENUM_AND_FLAGS(name, E, fields)                        \
  REN_REVERSE_MAP_ENUM(name, E, fields)                                        \
  constexpr auto name##Flags = detail::reverseMapFlags<E, name>

#define REN_DETAIL_DEFINE_STRINGIFY_CASE(r, data, e)                           \
  case e:                                                                      \
    return BOOST_PP_STRINGIZE(e);

#define REN_STRINGIFY_ENUM(E, fields)                                          \
  inline const char *to_string(E e) {                                          \
    using enum E;                                                              \
    switch (e) {                                                               \
    default: {                                                                 \
      if constexpr (detail::EnumWithUnknown<E>) {                              \
        return "Undefined";                                                    \
      } else {                                                                 \
        assert(!"Unhandled enum " BOOST_PP_STRINGIZE(E) " value");             \
        return "";                                                             \
      }                                                                        \
    }                                                                          \
      BOOST_PP_SEQ_FOR_EACH(REN_DETAIL_DEFINE_STRINGIFY_CASE, ~, fields)       \
    }                                                                          \
  }
} // namespace ren
