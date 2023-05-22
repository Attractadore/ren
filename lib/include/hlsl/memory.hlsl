#pragma once

template <typename T> T ptr_load(uint64_t base, uint idx = 0) {
  return vk::RawBufferLoad<T>(base + idx * sizeof(T));
}
