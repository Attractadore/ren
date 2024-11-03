#pragma once
#include "Support/StdDef.hpp"

#include <tiny_imageformat/tinyimageformat.h>
#include <vulkan/vulkan.h>

#ifndef REN_IMGUI
#define REN_IMGUI 0
#endif

namespace ren {

constexpr TinyImageFormat HDR_FORMAT = TinyImageFormat_R16G16B16A16_SFLOAT;
constexpr TinyImageFormat SDR_FORMAT = TinyImageFormat_R8G8B8A8_UNORM;
constexpr TinyImageFormat DEPTH_FORMAT = TinyImageFormat_D32_SFLOAT;
constexpr usize MAX_COLOR_ATTACHMENTS = 8;

constexpr usize DESCRIPTOR_TYPE_COUNT = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT + 1;
constexpr usize MAX_DESCIPTOR_BINDINGS = 16;
constexpr usize MAX_DESCRIPTOR_SETS = 4;

constexpr usize MAX_PUSH_CONSTANTS_SIZE = 128;

} // namespace ren
