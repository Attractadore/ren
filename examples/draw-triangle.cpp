#include "app-base.hpp"
#include "ren/ren.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

class DrawTriangleApp : public AppBase {
  ren::MeshID m_mesh;
  ren::MaterialID m_material;
  ren::MeshInstID m_model;
  ren::DirLightID m_light;

public:
  DrawTriangleApp() : AppBase("Draw Triangle") {
    auto &scene = get_scene();

    std::array<glm::vec3, 3> positions = {{
        {0.0f, 0.5f, 0.0f},
        {-std::sqrt(3.0f) / 4.0f, -0.25f, 0.0f},
        {std::sqrt(3.0f) / 4.0f, -0.25f, 0.0f},
    }};

    std::array<glm::vec3, 3> normals = {{
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
    }};

    std::array<glm::vec4, 3> colors = {{
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
    }};

    std::array<unsigned, 3> indices = {0, 1, 2};

    ren::MeshDesc mesh_desc = {};
    mesh_desc.set_positions(
        std::span(reinterpret_cast<const ren::Vector3 *>(positions.data()),
                  positions.size()));
    mesh_desc.set_normals(
        std::span(reinterpret_cast<const ren::Vector3 *>(normals.data()),
                  normals.size()));
    mesh_desc.set_colors(std::span(
        reinterpret_cast<const ren::Vector4 *>(colors.data()), colors.size()));
    mesh_desc.set_indices(indices);

    m_mesh = scene.create_mesh(mesh_desc).value();

    m_material = scene
                     .create_material({
                         .metallic_factor = 1.0f,
                         .roughness_factor = 0.5f,
                     })
                     .value();

    m_model = scene
                  .create_mesh_inst({
                      .mesh = m_mesh,
                      .material = m_material,
                  })
                  .value();

    // Ambient day light
    m_light = scene
                  .create_dir_light({
                      .color = {1.0f, 1.0f, 1.0f},
                      .illuminance = 25'000.0f,
                      .origin = {0.0f, 0.0f, 1.0f},
                  })
                  .value();
  }

protected:
  void iterate(unsigned width, unsigned height) override {
    auto &scene = get_scene();
    // Expose for ambient day light
    float iso = 400.0f;
    ren::CameraDesc desc = {
        .width = width,
        .height = height,
        .aperture = 8.0f,
        .shutter_time = 1.0f / iso,
        .iso = iso,
        .position = {0.0f, 0.0f, 1.0f},
        .forward = {0.0f, 0.0f, -1.0f},
        .up = {0.0f, 1.0f, 0.0f},
    };
    desc.set_projection(ren::OrthographicProjection{.width = 2.0f});
    scene.set_camera(desc).value();
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
