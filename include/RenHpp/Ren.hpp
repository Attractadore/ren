#pragma once
#include "Ren/Ren.h"

#include <utility>

namespace Ren {
class Device {
  RenDevice *m_device;

public:
  Device(RenDevice *device) : m_device(device) {}
  Device(const Device &) = delete;
  Device(Device &&other) : m_device(std::exchange(other.m_device, nullptr)) {}
  ~Device() { Ren_DestroyDevice(m_device); }

  Device &operator=(const Device &) = delete;
  Device &operator=(Device &&other) {
    Ren_DestroyDevice(m_device);
    m_device = other.m_device;
    other.m_device = nullptr;
    return *this;
  };
};
} // namespace Ren
