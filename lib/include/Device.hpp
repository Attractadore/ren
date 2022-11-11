#pragma once

#include "Ren/Ren.h"

struct RenDevice {
  virtual ~RenDevice() = default;
};

namespace Ren {
using Device = RenDevice;
}
