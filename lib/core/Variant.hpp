#pragma once
#include "TypeTraits.hpp"

#include <variant>

namespace ren {

template <class... Ts> struct OverloadSet : Ts... {
  using Ts::operator()...;
};

using Monostate = std::monostate;

template <typename... Ts> class Variant : public std::variant<Ts...> {
  using Base = std::variant<Ts...>;

public:
  using Base::Base;

  Variant(Base base) noexcept : Base(std::move(base)) {}

#define CVariantType(U) (std::same_as<U, Ts> or ...)

  template <typename... Fs, typename Self>
  decltype(auto) visit(this Self &self, Fs &&...funcs) {
    return std::visit(OverloadSet{std::forward<Fs>(funcs)...}, self);
  }

  template <typename T, typename Self>
    requires(CVariantType(T))
  auto get(this Self &self) -> ConstLikeT<T, Self> * {
    return std::get_if<T>(&self);
  }

  template <typename T>
    requires(CVariantType(T))
  auto get_or_emplace() -> T & {
    if (auto *ptr = std::get_if<T>(this)) {
      return *ptr;
    }
    return this->template emplace<T>();
  }

  explicit operator bool() const
    requires(CVariantType(Monostate))
  {
    return not std::holds_alternative<Monostate>(*this);
  }

#undef CVariantType
};

} // namespace ren
