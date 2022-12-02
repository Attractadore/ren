#pragma once
#include "Support/Errors.hpp"

#include <source_location>

#define DIRECTX12_UNIMPLEMENTED                                                \
  throw std::runtime_error(std::string("DirectX 12: ") +                       \
                           std::source_location::current().function_name() +   \
                           " not implemented!")
