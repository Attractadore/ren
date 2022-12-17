#pragma once
#include "Support/Queue.hpp"

#include <functional>

namespace ren {
template <typename D> struct DeviceTime;

template <typename D> using QueueDeleter = std::function<void(D &device)>;

template <typename D> class DeleteQueue {
  struct QueueItem {
    DeviceTime<D> time;
    QueueDeleter<D> deleter;
  };

  Queue<QueueItem> m_queue;

public:
  void push(DeviceTime<D> time, QueueDeleter<D> &&deleter) {
    m_queue.emplace(std::move(time), std::move(deleter));
  }

  void pop(D &device);

  void flush(D &device);
};

template <typename D> void DeleteQueue<D>::pop(D &device) {
  auto completed_time = device.getCompletedTime();
  while (not m_queue.empty()) {
    auto &&[time, deleter] = m_queue.front();
    if (time > completed_time) {
      break;
    }
    deleter(device);
    m_queue.pop();
  }
}

template <typename D> void DeleteQueue<D>::flush(D &device) {
  while (not m_queue.empty()) {
    m_queue.front().deleter(device);
    m_queue.pop();
  }
}
} // namespace ren
