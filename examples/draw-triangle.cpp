#include "AppBase.hpp"
#include "ren/baking/mesh.hpp"

#include <glm/glm.hpp>

class DrawTriangleApp : public AppBase {
public:
  DrawTriangleApp() : AppBase("Draw Triangle") {
    [&]() -> Result<void> {
      ren::IScene &scene = get_scene();

      glm::vec3 positions[] = {
          {0.0f, 0.5f, 0.0f},
          {-std::sqrt(3.0f) / 4.0f, -0.25f, 0.0f},
          {std::sqrt(3.0f) / 4.0f, -0.25f, 0.0f},
      };

      glm::vec4 colors[] = {
          {1.0f, 0.0f, 0.0f, 1.0f},
          {0.0f, 1.0f, 0.0f, 1.0f},
          {0.0f, 0.0f, 1.0f, 1.0f},
      };

      glm::vec3 normals[] = {
          {0.0f, 0.0f, 1.0f},
          {0.0f, 0.0f, 1.0f},
          {0.0f, 0.0f, 1.0f},
      };

      OK(auto blob, ren::bake_mesh_to_memory({
                        .num_vertices = 3,
                        .positions = positions,
                        .normals = normals,
                        .colors = colors,
                    }));
      auto [blob_data, blob_size] = blob;
      OK(ren::MeshId mesh, scene.create_mesh(blob_data, blob_size));
      std::free(blob_data);

      OK(ren::MaterialId material, scene.create_material({
                                       .roughness_factor = 0.5f,
                                       .metallic_factor = 1.0f,
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
