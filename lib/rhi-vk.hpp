#pragma once
#define REN_RHI_VULKAN 1
#if REN_RHI_VULKAN
#include "ren/core/StdDef.hpp"
#include "vma.hpp"

struct VolkDeviceTable;

namespace ren::rhi {

enum class QueueFamily;
enum class PipelineBindPoint;

namespace vk {

template <typename Self> struct HandleBase {
  explicit operator bool() const {
    return static_cast<const Self *>(this)->handle;
  }
};

template <typename Self>
bool operator==(const HandleBase<Self> &lhs, const HandleBase<Self> &rhs) {
  return static_cast<const Self &>(lhs).handle ==
         static_cast<const Self &>(rhs).handle;
};

struct InstanceData;

using Instance = const InstanceData *;

struct Adapter {
  u32 index = u32(-1);
};

struct DeviceData;

using Device = const DeviceData *;

struct Queue : HandleBase<Queue> {
  VkQueue handle = nullptr;
  const VolkDeviceTable *vk = nullptr;
};

struct Semaphore : HandleBase<Semaphore> {
  VkSemaphore handle = nullptr;
};

struct Allocation : HandleBase<Allocation> {
  VmaAllocation handle = nullptr;
};

struct Buffer : HandleBase<Buffer> {
  VkBuffer handle = nullptr;
  Allocation allocation = {};
};

struct Image : HandleBase<Image> {
  VkImage handle = nullptr;
  Allocation allocation = {};
};

struct ImageView : HandleBase<ImageView> {
  VkImageView handle = nullptr;
};

struct Sampler : HandleBase<Sampler> {
  VkSampler handle = nullptr;
};

struct Pipeline : HandleBase<Pipeline> {
  VkPipeline handle = nullptr;
};

struct Event : HandleBase<Event> {
  VkEvent handle = nullptr;
};

struct CommandPoolHeader {
  CommandPoolHeader *next = nullptr;
  QueueFamily queue_family = {};
};

struct CommandPoolData;

using CommandPool = CommandPoolHeader *;

struct CommandBuffer : HandleBase<CommandBuffer> {
  VkCommandBuffer handle = nullptr;
  Device device = {};
};

struct Surface : HandleBase<Surface> {
  VkSurfaceKHR handle = nullptr;
};

struct SwapChainData;

using SwapChain = SwapChainData *;

} // namespace vk

using namespace vk;

} // namespace ren::rhi

#endif // REN_RHI_VULKAN
