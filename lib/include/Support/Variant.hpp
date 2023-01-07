#pragma once

#include <variant>

namespace ren {

template <class... Ts> struct OverloadSet : Ts... {
  OverloadSet(Ts... fs) : Ts(std::move(fs))... {}
  using Ts::operator()...;
};

} // namespace ren
