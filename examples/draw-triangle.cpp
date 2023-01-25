#include "app-base.hpp"

#include <fmt/format.h>

class DrawTriangleApp : public AppBase {
  ren::UniqueMesh m_mesh;
  ren::UniqueMaterial m_material;
  ren::UniqueModel m_model;
  bool m_init = false;

public:
  DrawTriangleApp() : AppBase("Draw Triangle") {}

protected:
  void iterate(ren::Scene::Frame &scene) override {
    if (not m_init) {
      std::array<glm::vec3, 3> positions = {{
          {0.0f, 0.5f, 0.0f},
          {-std::sqrt(3.0f) / 4.0f, -0.25f, 0.0f},
          {std::sqrt(3.0f) / 4.0f, -0.25f, 0.0f},
      }};

      std::array<glm::vec3, 3> colors = {{
          {1.0f, 0.0f, 0.0f},
          {0.0f, 1.0f, 0.0f},
          {0.0f, 0.0f, 1.0f},
      }};

      std::array<unsigned, 3> indices = {0, 1, 2};

      m_mesh = scene->create_unique_mesh({
          .positions = positions,
          .colors = colors,
          .indices = indices,
      });

      m_material = scene->create_unique_material({
          .albedo = ren::VertexMaterialAlbedo(),
      });

      m_model = scene->create_unique_model({
          .mesh = m_mesh,
          .material = m_material,
      });

      m_init = true;
    }

    scene->get_camera().config({
        .projection_desc = ren::OrthographicCameraDesc{.width = 2.0f},
        .position = {0.0f, 0.0f, 1.0f},
        .forward = {0.0f, 0.0f, -1.0f},
        .up = {0.0f, 1.0f, 0.0f},
    });
  }
};

int main() {
  try {
    DrawTriangleApp().run();
  } catch (const std::exception &e) {
    fmt::print(stderr, "{}\n", e.what());
    return -1;
  }
}
