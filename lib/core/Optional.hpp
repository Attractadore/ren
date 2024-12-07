#pragma once
#include <tl/optional.hpp>

namespace ren {

template <typename T> using Optional = tl::optional<T>;

inline constexpr auto None = tl::nullopt;

} // namespace ren
