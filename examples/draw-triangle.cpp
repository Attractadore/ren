#include "AppBase.hpp"

#include <array>
#include <glm/glm.hpp>

class DrawTriangleApp : public AppBase {
public:
  DrawTriangleApp() : AppBase("Draw Triangle") {
    [&]() -> Result<void> {
      ren::IScene &scene = get_scene();

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

      OK(ren::MeshId mesh, scene.create_mesh({
                               .positions = positions,
                               .normals = normals,
                               .colors = colors,
                               .indices = indices,
                           }));

      OK(ren::MaterialId material, scene.create_material({
                                       .metallic_factor = 1.0f,
                                       .roughness_factor = 0.5f,
                                   }));

      OK(ren::MeshInstanceId model, scene.create_mesh_instance({
                                        .mesh = mesh,
                                        .material = material,
                                    }));

      // Ambient day light
      OK(ren::DirectionalLightId light, scene.create_directional_light({
                                            .color = {1.0f, 1.0f, 1.0f},
                                            .illuminance = 25'000.0f,
                                            .origin = {0.0f, 0.0f, 1.0f},
                                        }));

      ren::CameraId camera = get_camera();

      scene.set_camera_orthographic_projection(camera, {.width = 2.0f});
      scene.set_camera_transform(camera, {
                                             .position = {0.0f, 0.0f, 1.0f},
                                             .forward = {0.0f, 0.0f, -1.0f},
                                             .up = {0.0f, 1.0f, 0.0f},
                                         });

      scene.set_exposure({
          .mode = ren::ExposureMode::Automatic,
          .ec = 2.0f,
      });

      return {};
    }()
                 .transform_error(throw_error)
                 .value();
  }

  [[nodiscard]] static auto run() -> int {
    return AppBase::run<DrawTriangleApp>();
  }
};

int main() { return DrawTriangleApp::run(); }
