#pragma once
#include "Optional.hpp"
#include <variant>

namespace ren {

template <class... Ts> struct OverloadSet : Ts... {
  OverloadSet(Ts... fs) : Ts(std::move(fs))... {}
  using Ts::operator()...;
};

template <typename... Ts> struct Variant : std::variant<Ts...> {
public:
  using std::variant<Ts...>::variant;

  template <typename... Fs> auto visit(Fs &&...funcs) const {
    return std::visit(OverloadSet{std::forward<Fs>(funcs)...}, *this);
  }

  template <typename... Fs> auto visit(Fs &&...funcs) {
    return std::visit(OverloadSet{std::forward<Fs>(funcs)...}, *this);
  }

  template <typename T> auto get() const -> Optional<const T &> {
    if (const auto *ptr = std::get_if<T>(this)) {
      return *ptr;
    }
    return None;
  }

  template <typename T> auto get() -> Optional<T &> {
    if (auto *ptr = std::get_if<T>(this)) {
      return *ptr;
    }
    return None;
  }
};

} // namespace ren
