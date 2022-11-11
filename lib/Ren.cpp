#include "Device.hpp"

extern "C" {
void Ren_DestroyDevice(RenDevice *dev) { delete dev; }
}
