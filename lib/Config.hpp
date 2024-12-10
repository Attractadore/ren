#pragma once
#include "core/StdDef.hpp"

#include <vulkan/vulkan.h>

#ifndef REN_IMGUI
#define REN_IMGUI 0
#endif

namespace ren {

constexpr usize MAX_COLOR_ATTACHMENTS = 8;

constexpr usize DESCRIPTOR_TYPE_COUNT = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT + 1;
constexpr usize MAX_DESCIPTOR_BINDINGS = 16;
constexpr usize MAX_DESCRIPTOR_SETS = 4;

constexpr usize MAX_PUSH_CONSTANTS_SIZE = 256;

} // namespace ren
