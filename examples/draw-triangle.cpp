#include "AppBase.hpp"

#include <glm/glm.hpp>

class DrawTriangleApp : public AppBase {
public:
  DrawTriangleApp() : AppBase("Draw Triangle") {
    [&] -> Result<void> {
      auto &scene = get_scene();

      std::array<glm::vec3, 3> positions = {{
          {0.0f, 0.5f, 0.0f},
          {-std::sqrt(3.0f) / 4.0f, -0.25f, 0.0f},
          {std::sqrt(3.0f) / 4.0f, -0.25f, 0.0f},
      }};

      std::array<glm::vec4, 3> colors = {{
          {1.0f, 0.0f, 0.0f, 1.0f},
          {0.0f, 1.0f, 0.0f, 1.0f},
          {0.0f, 0.0f, 1.0f, 1.0f},
      }};

      std::array<glm::vec3, 3> normals = {{
          {0.0f, 0.0f, 1.0f},
          {0.0f, 0.0f, 1.0f},
          {0.0f, 0.0f, 1.0f},
      }};

      std::array<unsigned, 3> indices = {0, 1, 2};

      ren::MeshDesc mesh_desc = {};
      mesh_desc.set_positions(
          std::span(reinterpret_cast<const ren::Vector3 *>(positions.data()),
                    positions.size()));
      mesh_desc.set_normals(
          std::span(reinterpret_cast<const ren::Vector3 *>(normals.data()),
                    normals.size()));
      mesh_desc.set_colors(
          std::span(reinterpret_cast<const ren::Vector4 *>(colors.data()),
                    colors.size()));
      mesh_desc.set_indices(indices);

      OK(auto mesh, scene.create_mesh(mesh_desc));

      OK(auto material, scene.create_material({
                            .metallic_factor = 1.0f,
                            .roughness_factor = 0.5f,
                        }));

      OK(auto model, scene.create_mesh_inst({
                         .mesh = mesh,
                         .material = material,
                     }));

      // Ambient day light
      OK(auto light, scene.create_dir_light({
                         .color = {1.0f, 1.0f, 1.0f},
                         .illuminance = 25'000.0f,
                         .origin = {0.0f, 0.0f, 1.0f},
                     }));

      return {};
    }()
               .transform_error(throw_error);
  }

  [[nodiscard]] static auto run() -> int {
    return AppBase::run<DrawTriangleApp>();
  }

protected:
  auto iterate(unsigned width, unsigned height) -> Result<void> override {
    auto &scene = get_scene();
    ren::CameraDesc desc = {
        .width = width,
        .height = height,
        .exposure_compensation = 3.0f,
        .exposure_mode = REN_EXPOSURE_MODE_AUTOMATIC,
        .position = {0.0f, 0.0f, 1.0f},
        .forward = {0.0f, 0.0f, -1.0f},
        .up = {0.0f, 1.0f, 0.0f},
    };
    desc.set_projection(ren::OrthographicProjection{.width = 2.0f});
    TRY_TO(scene.set_camera(desc));
    return {};
  }
};

int main() { return DrawTriangleApp::run(); }
