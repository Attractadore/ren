#pragma once
#include "Hash.hpp"

#include <string>
#include <string_view>

namespace ren {

using String = std::string;
using StringView = std::string_view;

struct StringHash {
  using is_transparent = void;

  auto operator()(const char *str) const -> u64 {
    return (*this)(StringView(str));
  }

  auto operator()(StringView str) const -> u64 {
    return std::hash<std::string_view>()(str);
  }

  auto operator()(const String &str) const -> u64 {
    return (*this)(StringView(str));
  }
};

template <> struct Hash<String> : StringHash {};
template <> struct Hash<StringView> : StringHash {};

}; // namespace ren
