#if _WIN32
#include "ren/core/Chrono.hpp"

#include <Windows.h>

namespace ren {

namespace {

const u64 TSC_FREQUENCY = []() {
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
  return freq.QuadPart;
}();

} // namespace

u64 clock() {
  LARGE_INTEGER ticks;
  QueryPerformanceCounter(&ticks);
  return ticks.QuadPart * 1'000'000'000 / TSC_FREQUENCY;
}

} // namespace ren

#endif
