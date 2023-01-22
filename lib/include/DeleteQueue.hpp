#pragma once
#include "Config.hpp"
#include "Support/Queue.hpp"
#include "Support/TypeMap.hpp"
#include "Vulkan/VMA.h"

#include <cassert>
#include <functional>

namespace ren {
class Device;

namespace detail {
template <typename T, size_t Idx, typename... Ts>
constexpr bool IsQueueTypeHelper = [] {
  if constexpr (Idx >= sizeof...(Ts)) {
    return false;
  } else if constexpr (std::same_as<
                           T, std::tuple_element_t<Idx, std::tuple<Ts...>>>) {
    return true;
  } else {
    return IsQueueTypeHelper<T, Idx + 1, Ts...>;
  }
}();
}

template <typename T, typename... Ts>
concept IsQueueType = detail::IsQueueTypeHelper<T, 0, Ts...>;

using QueueCustomDeleter = std::function<void(Device &device)>;

template <typename T> struct QueueDeleter {
  void operator()(Device &device, T value) const noexcept;
};

template <> struct QueueDeleter<QueueCustomDeleter> {
  void operator()(Device &device, QueueCustomDeleter deleter) const noexcept {
    deleter(device);
  }
};

namespace detail {
template <typename... Ts> class DeleteQueueImpl {
  struct FrameData {
    TypeMap<unsigned, Ts...> pushed_item_counts;
  };

  std::tuple<Queue<Ts>...> m_queues;
  std::array<FrameData, c_pipeline_depth> m_frame_data;
  unsigned m_frame_idx = 0;

private:
  template <typename T> Queue<T> &get_queue() {
    return std::get<Queue<T>>(m_queues);
  }
  template <typename T> unsigned &get_frame_pushed_item_count() {
    return m_frame_data[m_frame_idx].pushed_item_counts.template get<T>();
  }

  template <typename T> void push_impl(T value) {
    get_queue<T>().push(std::move(value));
    get_frame_pushed_item_count<T>()++;
  }

  template <typename T> void pop(Device &device, unsigned count) {
    auto &queue = get_queue<T>();
    for (int i = 0; i < count; ++i) {
      assert(not queue.empty());
      QueueDeleter<T>()(device, std::move(queue.front()));
      queue.pop();
    }
  }

public:
  void begin_frame(Device &device) {
    // Increment the frame index when a new frame is begin. If this is done when
    // a frame ends, shared_ptrs that go out of scope after will add their
    // resources to the frame that is about to begin and will be destroyed right
    // away, rather than after they are no longer in use.
    m_frame_idx = (m_frame_idx + 1) % c_pipeline_depth;
    (pop<Ts>(device, get_frame_pushed_item_count<Ts>()), ...);
    m_frame_data[m_frame_idx] = {};
  }
  void end_frame(Device &device) {}

  template <IsQueueType<Ts...> T> void push(T value) {
    push_impl(std::move(value));
  }

  template <std::convertible_to<QueueCustomDeleter> F>
    requires IsQueueType<QueueCustomDeleter, Ts...> and
             (not std::same_as<QueueCustomDeleter, F>)
  void push(F callback) {
    push_impl(QueueCustomDeleter(std::move(callback)));
  }

  void flush(Device &device) {
    (pop<Ts>(device, get_queue<Ts>().size()), ...);
    m_frame_data.fill({});
  }
};
} // namespace detail

struct ImageViews {
  VkImage image;
};

using DeleteQueue =
    detail::DeleteQueueImpl<QueueCustomDeleter, ImageViews, VkBuffer,
                            VkDescriptorPool, VkDescriptorSetLayout, VkImage,
                            VkPipeline, VkPipelineLayout, VkSemaphore,
                            VkSwapchainKHR, VmaAllocation>;

} // namespace ren
