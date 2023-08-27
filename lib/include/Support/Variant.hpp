#pragma once
#include "Optional.hpp"
#include <variant>

namespace ren {

template <class... Ts> struct OverloadSet : Ts... {
  OverloadSet(Ts... fs) : Ts(std::move(fs))... {}
  using Ts::operator()...;
};

namespace detail {

template <typename T, typename... Ts>
concept CSameAsAnyOf = (std::same_as<T, Ts> or ...);

};

using Monostate = std::monostate;

template <typename... Ts> struct Variant : std::variant<Ts...> {
public:
  using std::variant<Ts...>::variant;

#define CVariantType(U) detail::CSameAsAnyOf<U, Ts...>

  template <typename... Fs> auto visit(Fs &&...funcs) const {
    return std::visit(OverloadSet{std::forward<Fs>(funcs)...}, *this);
  }

  template <typename... Fs> auto visit(Fs &&...funcs) {
    return std::visit(OverloadSet{std::forward<Fs>(funcs)...}, *this);
  }

  template <typename T>
    requires CVariantType(T)
  auto get() const -> Optional<const T &> {
    if (const auto *ptr = std::get_if<T>(this)) {
      return *ptr;
    }
    return None;
  }

  template <typename T>
    requires CVariantType(T)
  auto get() -> Optional<T &> {
    if (auto *ptr = std::get_if<T>(this)) {
      return *ptr;
    }
    return None;
  }

  template <typename T>
    requires CVariantType(T)
  auto get_or_emplace() -> T & {
    if (auto *ptr = std::get_if<T>(this)) {
      return *ptr;
    }
    return this->template emplace<T>();
  }

  explicit operator bool() const
    requires CVariantType(Monostate)
  {
    return not std::holds_alternative<Monostate>(*this);
  }

#undef CVariantType
};

} // namespace ren
