#pragma once
#include "Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/reciprocal.hpp>

namespace ren {

template <typename T>
constexpr auto infinitePerspectiveRH_ReverseZ(T fovy, T aspect, T zNear) {
  assert(glm::abs(aspect - std::numeric_limits<T>::epsilon()) >
         static_cast<T>(0));
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

inline glm::mat4 get_view_matrix(const Camera &camera) {
  return glm::lookAt(camera.position, camera.position + camera.forward,
                     camera.up);
}

inline glm::mat4 get_projection_matrix(const Camera &camera,
                                       float aspect_ratio) {
  return camera.projection.visit(OverloadSet{
      [&](const PerspectiveProjection &proj) {
        float fov = proj.hfov / aspect_ratio;
        return infinitePerspectiveRH_ReverseZ(fov, aspect_ratio, 0.01f);
      },
      [&](const OrthographicProjection &proj) {
        float width = proj.width;
        float height = width / aspect_ratio;
        return orthoRH_ReverseZ(width, height, 0.01f, 100.0f);
      },
  });
}

inline glm::mat4 get_projection_matrix(const Camera &camera,
                                       glm::uvec2 viewport) {
  return get_projection_matrix(camera, float(viewport.x) / float(viewport.y));
}

} // namespace ren
