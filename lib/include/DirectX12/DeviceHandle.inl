#pragma once
#include "DeviceHandle.hpp"
#include "DirectX12Device.hpp"

#include <cassert>

namespace ren {
namespace detail {
inline void IUnknownDeleter::operator()(IUnknown *ptr) const noexcept {
  if (ptr) {
    assert(m_device);
    m_device->pushToDeleteQueue([ptr](DirectX12Device &) { ptr->Release(); });
  }
}
}; // namespace detail
} // namespace ren
