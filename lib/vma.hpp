#pragma once
#include <vulkan/vulkan.h>
// Include after vulkan.h
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_STATS_STRING_ENABLED 0
#define VMA_CALL_PRE __attribute__((visibility("default")))
#include <vk_mem_alloc.h>
