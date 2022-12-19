#pragma once
#include "DeviceHandle.hpp"
#include "DirectX12Device.hpp"

#include <cassert>

namespace ren {
namespace detail {
inline void IUnknownDeleter::operator()(IUnknown *handle) const noexcept {
  if (handle) {
    assert(m_device);
    m_device->push_to_delete_queue(handle);
  }
}
}; // namespace detail
} // namespace ren
