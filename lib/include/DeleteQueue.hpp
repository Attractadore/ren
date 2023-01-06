#pragma once
#include "Config.hpp"
#include "Support/Queue.hpp"
#include "Support/TypeMap.hpp"

#include <cassert>
#include <functional>

namespace ren {
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

template <typename D> using QueueCustomDeleter = std::function<void(D &device)>;

template <typename D, typename T> struct QueueDeleter {
  void operator()(D &device, T value) const noexcept;
};

template <typename D> struct QueueDeleter<D, QueueCustomDeleter<D>> {
  void operator()(D &device, QueueCustomDeleter<D> deleter) const noexcept {
    deleter(device);
  }
};

template <typename D, typename... Ts> class DeleteQueue {
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

  template <typename T> void pop(D &device, unsigned count) {
    auto &queue = get_queue<T>();
    for (int i = 0; i < count; ++i) {
      assert(not queue.empty());
      QueueDeleter<D, T>()(device, std::move(queue.front()));
      queue.pop();
    }
  }

public:
  void begin_frame(D &device) {
    // Increment the frame index when a new frame is begin. If this is done when
    // a frame ends, shared_ptrs that go out of scope after will add their
    // resources to the frame that is about to begin and will be destroyed right
    // away, rather than after they are no longer in use.
    m_frame_idx = (m_frame_idx + 1) % c_pipeline_depth;
    (pop<Ts>(device, get_frame_pushed_item_count<Ts>()), ...);
    m_frame_data[m_frame_idx] = {};
  }
  void end_frame(D &device) {}

  template <IsQueueType<Ts...> T> void push(T value) {
    push_impl(std::move(value));
  }

  template <std::convertible_to<QueueCustomDeleter<D>> F>
  requires IsQueueType<QueueCustomDeleter<D>, Ts...> and
      (not std::same_as<QueueCustomDeleter<D>, F>) void push(F callback) {
    push_impl(QueueCustomDeleter<D>(std::move(callback)));
  }

  void flush(D &device) {
    (pop<Ts>(device, get_queue<Ts>().size()), ...);
    m_frame_data.fill({});
  }
};
} // namespace ren
