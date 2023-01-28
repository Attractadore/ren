#include "app-base.hpp"

#include <fmt/format.h>

class DrawTriangleApp : public AppBase {
  ren::Model m_model;

public:
  DrawTriangleApp() : AppBase("Draw Triangle") {}

protected:
  void iterate(ren::Scene::Frame &scene) override {
    if (!m_model) {
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

      auto mesh = ren::Mesh(scene, {
                                       .positions = positions,
                                       .colors = colors,
                                       .indices = indices,
                                   });

      auto material =
          ren::Material(scene, {
                                   .albedo = ren::VertexMaterialAlbedo(),
                               });

      m_model = ren::Model(scene, std::move(mesh), std::move(material));
    }

    scene->set_camera({
        .projection = ren::OrthographicProjection{.width = 2.0f},
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
