#include "Camera.hpp"
#include "ren/core/Assert.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/reciprocal.hpp>
#include <utility>

namespace ren {

template <typename T>
constexpr auto infinitePerspectiveRH_ReverseZ(T fovy, T aspect, T zNear) {
  ren_assert(glm::abs(aspect - std::numeric_limits<T>::epsilon()) >
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

glm::mat4 get_view_matrix(const Camera &camera) {
  return glm::lookAt(camera.position, camera.position + camera.forward,
                     camera.up);
}

glm::mat4 get_projection_matrix(const Camera &camera, float aspect_ratio) {
  switch (camera.proj) {
  case CameraProjection::Perspective: {
    float fov = camera.persp_hfov / aspect_ratio;
    return infinitePerspectiveRH_ReverseZ(fov, aspect_ratio, camera.near);
  }
  case CameraProjection::Orthograpic: {
    float width = camera.ortho_width;
    float height = width / aspect_ratio;
    return orthoRH_ReverseZ(width, height, camera.near, camera.far);
  }
  }
  unreachable();
}

glm::mat4 get_projection_matrix(const Camera &camera, glm::uvec2 viewport) {
  return get_projection_matrix(camera, float(viewport.x) / float(viewport.y));
}

auto get_projection_view_matrix(const Camera &camera, glm::uvec2 viewport)
    -> glm::mat4 {
  return get_projection_matrix(camera, viewport) * get_view_matrix(camera);
}

} // namespace ren
