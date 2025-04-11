#pragma once
#if REN_RHI_VULKAN
#include "core/StdDef.hpp"

#include <vulkan/vulkan.h>
// Include after vulkan.h
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_STATS_STRING_ENABLED 0
#include <vk_mem_alloc.h>

struct VolkDeviceTable;

namespace ren::rhi {

enum class QueueFamily;
enum class PipelineBindPoint;

namespace vk {

struct HandleBase {
  explicit operator bool(this const auto &self) { return self.handle; }
};

struct Adapter {
  u32 index = u32(-1);
};

struct DeviceData;

using Device = const DeviceData *;

struct Queue : HandleBase {
  VkQueue handle = nullptr;
  const VolkDeviceTable *vk = nullptr;
};

struct Semaphore : HandleBase {
  VkSemaphore handle = nullptr;
};

struct Allocation : HandleBase {
  VmaAllocation handle = nullptr;
};

struct Buffer : HandleBase {
  VkBuffer handle = nullptr;
  Allocation allocation = {};
};

struct Image : HandleBase {
  VkImage handle = nullptr;
  Allocation allocation = {};
};

struct ImageView : HandleBase {
  VkImageView handle = nullptr;
};

struct Sampler : HandleBase {
  VkSampler handle = nullptr;
};

struct Pipeline : HandleBase {
  VkPipeline handle = nullptr;
};

struct CommandPoolData;

using CommandPool = CommandPoolData *;

struct CommandBuffer : HandleBase {
  VkCommandBuffer handle = nullptr;
  Device device = {};
};

struct Surface : HandleBase {
  VkSurfaceKHR handle = nullptr;
};

struct SwapChainData;

using SwapChain = SwapChainData *;

} // namespace vk

using namespace vk;

} // namespace ren::rhi

#endif // REN_RHI_VULKAN
