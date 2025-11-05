#pragma once

#if __clang__
#pragma clang system_header
#endif

#if __GNUC__
#pragma GCC system_header
#endif

#include "ren-vma_export.h"

#include <vulkan/vulkan.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_STATS_STRING_ENABLED 0

#define VMA_CALL_PRE REN_VMA_EXPORT

// Include after vulkan.h
#include <vk_mem_alloc.h>
