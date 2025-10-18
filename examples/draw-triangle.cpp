#include "AppBase.hpp"
#include "ren/baking/mesh.hpp"

#include <glm/glm.hpp>

class DrawTriangleApp : public AppBase {
  ren::Handle<ren::Mesh> m_mesh;
  ren::Handle<ren::Material> m_material;
  ren::Handle<ren::MeshInstance> m_triangle;

public:
  auto init() -> Result<void> {
    TRY_TO(AppBase::init("Draw Triangle"));

    ren::Scene *scene = get_scene();

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
    m_mesh = create_mesh(&m_frame_arena, scene, blob_data, blob_size);
    std::free(blob_data);

    m_material = ren::create_material(&m_frame_arena, scene,
                                      {
                                          .roughness_factor = 0.5f,
                                          .metallic_factor = 1.0f,
                                      });

    // Ambient day light
    ren::Handle<ren::DirectionalLight> light =
        ren::create_directional_light(scene, {
                                                 .color = {1.0f, 1.0f, 1.0f},
                                                 .illuminance = 25'000.0f,
                                                 .origin = {0.0f, 0.0f, 1.0f},
                                             });

    ren::Handle<ren::Camera> camera = get_camera();

    ren::set_camera_orthographic_projection(scene, camera, {.width = 2.0f});
    ren::set_camera_transform(scene, camera,
                              {
                                  .position = {0.0f, 0.0f, 1.0f},
                                  .forward = {0.0f, 0.0f, -1.0f},
                                  .up = {0.0f, 1.0f, 0.0f},
                              });

    return {};
  }

  [[nodiscard]] Result<void> process_frame(std::chrono::nanoseconds) override {
    ren::Scene *scene = get_scene();
    ren::destroy_mesh_instance(&m_frame_arena, scene, m_triangle);
    m_triangle =
        ren::create_mesh_instance(&m_frame_arena, scene, {m_mesh, m_material});
    ren::set_mesh_instance_transform(&m_frame_arena, scene, m_triangle,
                                     glm::mat4(1.0f));
    return {};
  }

  [[nodiscard]] static auto run() -> int {
    return AppBase::run<DrawTriangleApp>();
  }
};

int main() { return DrawTriangleApp::run(); }
