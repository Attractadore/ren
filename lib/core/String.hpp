#pragma once
#include <string>
#include <string_view>

namespace ren {

using String = std::string;
using StringView = std::string_view;

struct DummyString {
  DummyString() = default;
  DummyString(const char *) {}
  DummyString(const String &) {}
  DummyString(StringView) {}

  bool operator==(const DummyString &) const = default;

  const char *c_str() const { return nullptr; }
};

}; // namespace ren
