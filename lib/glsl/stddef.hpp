#pragma once
#include "common.hpp"

GLSL_NAMESPACE_BEGIN

constexpr uint MAX_PUSH_CONSTANS_SIZE = 128;
#define GLSL_PUSH_CONSTANTS(def, name)                                         \
  struct name def;                                                             \
  static_assert(sizeof(name) <= MAX_PUSH_CONSTANS_SIZE)

GLSL_NAMESPACE_END
