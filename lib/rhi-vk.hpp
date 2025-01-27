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

struct Adapter {
  u32 index = -1;
};

struct DeviceData;

using Device = const DeviceData *;

auto get_vk_device(Device device) -> VkDevice;

struct Queue {
  VkQueue handle = nullptr;
  const VolkDeviceTable *vk = nullptr;
};

struct Semaphore {
  VkSemaphore handle = nullptr;
};

struct Allocation {
  VmaAllocation handle = nullptr;

public:
  explicit operator bool() const { return handle; }
};

struct Buffer {
  VkBuffer handle = nullptr;
  Allocation allocation = {};
};

struct Image {
  VkImage handle = nullptr;
  Allocation allocation = {};
};

struct SRV {
  VkImageView handle = nullptr;
};

struct UAV {
  VkImageView handle = nullptr;
};

struct RTV {
  VkImageView handle = nullptr;
};

struct Sampler {
  VkSampler handle = nullptr;
};

#define REN_RHI_MUTABLE_DESCRIPTORS 0

struct ResourceDescriptorHeap {
  VkDescriptorPool pool = nullptr;
  VkDescriptorSet sets[3] = {};
};

struct SamplerDescriptorHeap {
  VkDescriptorPool pool = nullptr;
  VkDescriptorSet set = nullptr;
};

struct PipelineLayout {
  VkPipelineLayout handle = nullptr;
};

struct Pipeline {
  VkPipeline handle = nullptr;
};

struct CommandPoolData;

using CommandPool = CommandPoolData *;

struct CommandBuffer {
  VkCommandBuffer handle = nullptr;
  Device device = {};

public:
  explicit operator bool() const { return handle; }
};

struct Surface {
  VkSurfaceKHR handle = nullptr;
};

struct SwapChainData;

using SwapChain = SwapChainData *;

} // namespace vk

using namespace vk;

} // namespace ren::rhi

#endif // REN_RHI_VULKAN
