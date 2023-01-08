#pragma once
#include "Support/Errors.hpp"

#include <comdef.h>
#include <fmt/format.h>

namespace ren {
[[noreturn]] inline void
dx12Unimplemented(std::source_location sl = std::source_location::current()) {
  throw std::runtime_error(fmt::format("DirectX 12: {}:{}: {} not implemented!",
                                       sl.file_name(), sl.line(),
                                       sl.function_name()));
}

template <>
inline void throwIfFailed<HRESULT>(HRESULT hres, const char *message) {
  if (FAILED(hres)) {
    throw std::runtime_error(
        fmt::format("{}: {}", message, _com_error(hres).ErrorMessage()));
  }
}

} // namespace ren

#define DIRECTX12_UNIMPLEMENTED ren::dx12Unimplemented()
