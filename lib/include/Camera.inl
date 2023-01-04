#pragma once
#include "Camera.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/reciprocal.hpp>

namespace ren {

template <typename T>
constexpr auto infinitePerspectiveRH_ReverseZ(T fovy, T aspect, T zNear) {
  assert(abs(aspect - std::numeric_limits<T>::epsilon()) > static_cast<T>(0));
  auto cotHalfFovy = glm::cot(fovy / static_cast<T>(2));
  auto result = glm::zero<glm::mat<4, 4, T, glm::defaultp>>();
  result[0][0] = cotHalfFovy / aspect;
  result[1][1] = cotHalfFovy;
  result[2][3] = -static_cast<T>(1);
  result[3][2] = zNear;
  return result;
}

template <typename T>
constexpr auto orthoRH_ReverseZ(T width, T height, T zNear, T zFar) {
  auto result = glm::zero<glm::mat<4, 4, T, glm::defaultp>>();
  result[0][0] = static_cast<T>(2) / width;
  result[1][1] = static_cast<T>(2) / height;
  result[2][2] = -static_cast<T>(1) / (zNear - zFar);
  result[3][2] = -zFar / (zNear - zFar);
  result[3][3] = static_cast<T>(1);
  return result;
}

} // namespace ren
