#pragma once
#include "Support/Errors.hpp"

#include <fmt/format.h>

#include <source_location>

namespace ren {
[[noreturn]] inline void
dx12Unimplemented(std::source_location sl = std::source_location::current()) {
  throw std::runtime_error(fmt::format("DirectX 12: {}:{}: {} not implemented!",
                                       sl.file_name(), sl.line(),
                                       sl.function_name()));
}
} // namespace ren

#define DIRECTX12_UNIMPLEMENTED ren::dx12Unimplemented()
