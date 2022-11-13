#pragma once
#include <memory>

namespace ren {
template <typename T> using Unique = std::unique_ptr<T>;

template <typename T> using Ref = std::shared_ptr<T>;
} // namespace ren
