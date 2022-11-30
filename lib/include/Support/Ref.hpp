#pragma once
#include <memory>

namespace ren {
template <typename T> using Ref = std::shared_ptr<T>;

using AnyRef = Ref<void>;
} // namespace ren
