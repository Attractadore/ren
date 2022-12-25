#include "app-base.hpp"

class DrawTriangleApp : public AppBase {
  ren::UniqueMesh m_mesh;
  ren::UniqueMaterial m_material;
  ren::UniqueModel m_model;

public:
  DrawTriangleApp() : AppBase("Draw Triangle") {
    std::array<glm::vec3, 3> positions = {{
        {0.0f, 0.5f, 0.0f},
        {0.0f, 0.5f, 0.0f},
        {0.0f, 0.5f, 0.0f},
    }};
    std::array<glm::vec3, 3> colors = {{
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    }};
    std::array<unsigned, 3> indices = {0, 1, 2};
    m_mesh = get_scene().create_unique_mesh({
        .positions = positions,
        .colors = colors,
        .indices = indices,
    });
    m_material = get_scene().create_unique_material(
        {.albedo = ren::VertexMaterialAlbedo()});
    m_model = get_scene().create_unique_model(
        {.mesh = m_mesh.get(), .material = m_material.get()});
  }

protected:
  void iterate() override {
    get_scene().get_camera().config({
        .projection_desc = ren::OrthographicCameraDesc{.width = 2.0f},
        .aspect_ratio = get_window_aspect_ratio(),
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
    std::cerr << e.what() << "\n";
    return -1;
  }
}
