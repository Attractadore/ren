#pragma once
#include <expected>

namespace ren {

template <typename T, typename E> using Result = std::expected<T, E>;

template <typename E> using Failed = std::unexpected<E>;

} // namespace ren
