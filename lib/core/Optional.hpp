#pragma once
#include <optional>

namespace ren {

template <typename T> using Optional = std::optional<T>;

inline constexpr auto None = std::nullopt;

} // namespace ren
