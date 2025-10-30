#include "ren/core/Assert.hpp"

#include <fmt/base.h>

namespace ren {

void assert_msg(std::source_location sl, const char *condition,
                const char *msg) {
  if (msg) {
    fmt::println(stderr, "{}:{}: {}: Assertion \"{}\" failed: {}",
                 sl.file_name(), sl.line(), sl.function_name(), condition, msg);
  } else {
    fmt::println(stderr, "{}:{}: {}: Assertion \"{}\" failed", sl.file_name(),
                 sl.line(), sl.function_name(), condition);
  }
}

void todo_msg(std::source_location sl, const char *msg) {
  fmt::println(stderr, "{}:{}: {}: {} not implemented!", sl.file_name(),
               sl.line(), sl.function_name(), msg);
}

} // namespace ren
