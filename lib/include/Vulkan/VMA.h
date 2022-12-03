#pragma once
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "VmaUsage.h"
// FIXME: Windows :/
#undef CreateSemaphore
