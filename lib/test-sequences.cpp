#include "core/Views.hpp"
#include "ren/core/StdDef.hpp"
#include "sh/Random.h"

#include <fmt/base.h>

using namespace ren;

int main() {
  const u32 NUM_CORPUT_BASE_2 = 16;
  fmt::println("First {} base 2 van der Corput sequences terms:",
               NUM_CORPUT_BASE_2);
  for (u32 i : range<u32>(1, NUM_CORPUT_BASE_2 + 1)) {
    fmt::println("{}: {}", i, sh::corput_base_2(i));
  }

  const u32 NUM_CORPUT_BASE_3 = 16;
  fmt::println("First {} base 3 van der Corput sequences terms:",
               NUM_CORPUT_BASE_3);
  for (u32 i : range<u32>(1, NUM_CORPUT_BASE_3 + 1)) {
    fmt::println("{}: {}", i, sh::corput_base_3(i));
  }

  const u32 NUM_HAMMERSLEY_2D = 16;
  fmt::println("{} 2D Hammersley sequences terms:", NUM_HAMMERSLEY_2D);
  for (u32 i : range<u32>(1, NUM_HAMMERSLEY_2D + 1)) {
    glm::vec2 xy = sh::hammersley_2d(i, NUM_HAMMERSLEY_2D);
    fmt::println("{}: ({}, {})", i, xy.x, xy.y);
  }

  const u32 NUM_HAMMERSLEY_3D = 16;
  fmt::println("{} 3D Hammersley sequences terms:", NUM_HAMMERSLEY_3D);
  for (u32 i : range<u32>(1, NUM_HAMMERSLEY_3D + 1)) {
    glm::vec3 xyz = sh::hammersley_3d(i, NUM_HAMMERSLEY_3D);
    fmt::println("{}: ({}, {}, {})", i, xyz.x, xyz.y, xyz.z);
  }
}
