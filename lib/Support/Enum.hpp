#pragma once
#include "Flags.hpp"

#include <boost/preprocessor/seq.hpp>
#include <boost/preprocessor/stringize.hpp>

#include <cstddef>
#include <cstdint>

namespace ren {
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

} // namespace ren
