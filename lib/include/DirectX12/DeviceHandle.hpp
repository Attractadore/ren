#pragma once
#include "DirectX12DeleteQueue.hpp"

#include <unknwn.h>

#include <memory>

namespace ren {
namespace detail {
class IUnknownDeleter {
  DirectX12Device *m_device;

public:
  IUnknownDeleter(DirectX12Device *device = nullptr) : m_device(device) {}
  void operator()(IUnknown *handle) const noexcept;
};
} // namespace detail

template <typename T>
struct DirectX12DeviceHandle : std::unique_ptr<T, detail::IUnknownDeleter> {
  DirectX12DeviceHandle() = default;
  DirectX12DeviceHandle(T *ptr, DirectX12Device *device)
      : std::unique_ptr<T, detail::IUnknownDeleter>::unique_ptr(
            ptr, detail::IUnknownDeleter(device)) {}
};
} // namespace ren
