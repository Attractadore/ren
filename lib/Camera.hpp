#pragma once
#include <glm/glm.hpp>

namespace ren {

enum class CameraProjection {
  Perspective,
  Orthograpic,
};

struct CameraParameters {
  float aperture = 16.0f;
  float shutter_time = 1.0f / 400.0f;
  float iso = 400.0f;
};

struct Camera {
  glm::vec3 position;
  glm::vec3 forward = {1.0f, 0.0f, 0.0f};
  glm::vec3 up = {0.0f, 0.0f, 1.0f};
  CameraProjection proj = CameraProjection::Perspective;
  float persp_hfov = glm::radians(90.0f);
  float ortho_width = 1.0f;
  float near = 0.01f;
  float far = 0.0f;
  CameraParameters params;
};

glm::mat4 get_view_matrix(const Camera &camera);

glm::mat4 get_projection_matrix(const Camera &camera, float aspect_ratio);

glm::mat4 get_projection_matrix(const Camera &camera, glm::uvec2 viewport);

auto get_projection_view_matrix(const Camera &camera,
                                glm::uvec2 viewport) -> glm::mat4;

} // namespace ren
