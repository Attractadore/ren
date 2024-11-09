#include "ImGuiApp.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cmath>
#include <cstdlib>
#include <cxxopts.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>
#include <random>

namespace fs = std::filesystem;

namespace {

auto load_mesh(ren::IScene &scene, const char *path) -> Result<ren::MeshId> {
  Assimp::Importer importer;
  importer.SetPropertyBool(AI_CONFIG_PP_PTV_NORMALIZE, true);
  const aiScene *ai_scene = importer.ReadFile(
      path,
      // clang-format off
      aiProcess_Triangulate |
      aiProcess_GenNormals |
      aiProcess_PreTransformVertices |
      aiProcess_SortByPType |
      aiProcess_FindInvalidData
      // clang-format on
  );
  if (!ai_scene) {
    return Err(importer.GetErrorString());
  }
  assert(ai_scene->mNumMeshes > 0);
  const aiMesh *mesh = ai_scene->mMeshes[0];
  assert(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE);
  assert(mesh->HasPositions());
  static_assert(sizeof(glm::vec3) == sizeof(*mesh->mVertices));
  assert(mesh->HasNormals());
  static_assert(sizeof(glm::vec3) == sizeof(*mesh->mNormals));
  static_assert(sizeof(glm::vec4) == sizeof(*mesh->mColors[0]));
  std::vector<unsigned> indices(mesh->mNumFaces * 3);
  for (size_t f = 0; f < mesh->mNumFaces; ++f) {
    assert(mesh->mFaces[f].mNumIndices == 3);
    for (size_t i = 0; i < 3; ++i) {
      indices[f * 3 + i] = mesh->mFaces[f].mIndices[i];
    }
  }
  return scene
      .create_mesh({
          .positions =
              std::span(reinterpret_cast<const glm::vec3 *>(mesh->mVertices),
                        mesh->mNumVertices),
          .normals =
              std::span(reinterpret_cast<const glm::vec3 *>(mesh->mNormals),
                        mesh->mNumVertices),
          .colors =
              std::span(reinterpret_cast<const glm::vec4 *>(mesh->mColors[0]),
                        mesh->HasVertexColors(0) ? mesh->mNumVertices : 0),
          .indices = indices,
      })
      .transform_error(get_error_string);
}

auto create_material(ren::IScene &scene) -> Result<ren::MaterialId> {
  return scene
      .create_material({
          .metallic_factor = 0.0f,
      })
      .transform_error(get_error_string);
}

auto get_scene_bounds(unsigned num_entities) -> std::tuple<float, float> {
  float s = std::cbrt(num_entities);
  return {-s, s};
}

auto init_random(unsigned seed) -> std::mt19937 {
  return std::mt19937(seed ? seed : std::random_device()());
}

auto random_transform(std::mt19937 &rg, float min_trans, float max_trans,
                      float min_scale, float max_scale) -> glm::mat4x3 {
  std::uniform_real_distribution<float> trans_dist(min_trans, max_trans);
  std::uniform_int_distribution<int> axis_dist(INT_MIN, INT_MAX);
  std::uniform_real_distribution<float> angle_dist(0.0f,
                                                   2.0f * std::numbers::pi);
  std::uniform_real_distribution<float> scale_dist(min_scale, max_scale);

  glm::vec3 translate;
  for (int i = 0; i < 3; ++i) {
    translate[i] = trans_dist(rg);
  }

  glm::vec3 axis;
  for (int i = 0; i < 3; ++i) {
    axis[i] = axis_dist(rg);
  }
  axis = normalize(axis);
  float angle = angle_dist(rg);

  glm::vec3 scale;
  for (int i = 0; i < 3; ++i) {
    scale[i] = scale_dist(rg);
  }

  glm::mat4 transform(1.0f);
  transform = glm::translate(transform, translate);
  transform = glm::rotate(transform, angle, axis);
  transform = glm::scale(transform, scale);

  return transform;
}

auto place_entities(std::mt19937 &rg, ren::IScene &scene,
                    std::vector<glm::mat4x3> &transforms) -> Result<void> {
  unsigned num_entities = transforms.size();

  auto [min_trans, max_trans] = get_scene_bounds(num_entities);
  float min_scale = 0.5f;
  float max_scale = 1.0f;

  for (glm::mat4x3 &transform : transforms) {
    transform =
        random_transform(rg, min_trans, max_trans, min_scale, max_scale);
  }

  return {};
}

auto place_light(ren::IScene &scene) -> Result<void> {
  OK(auto _, scene
                 .create_directional_light({
                     .origin = {-1.0f, 0.0f, 1.0f},
                 })
                 .transform_error(get_error_string));
  return {};
}

void set_camera(ren::IScene &scene, ren::CameraId camera,
                unsigned num_entities) {
  auto [scene_min, _] = get_scene_bounds(num_entities);

  scene.set_camera_perspective_projection(camera, {});
  scene.set_camera_transform(camera, {
                                         .position = {scene_min, 0.0f, 0.0f},
                                         .forward = {1.0f, 0.0f, 0.0f},
                                         .up = {0.0f, 0.0f, 1.0f},
                                     });

  scene.set_exposure({
      .mode = ren::ExposureMode::Automatic,
      .ec = 2.0f,
  });
}

} // namespace

class EntityStressTestApp : public ImGuiApp {
public:
  EntityStressTestApp(const char *mesh_path, unsigned num_entities,
                      unsigned seed)
      : ImGuiApp(
            fmt::format("Entity Stress Test: {} @ {}", mesh_path, num_entities)
                .c_str()) {
    [&]() -> Result<> {
      ren::IScene &scene = get_scene();
      ren::CameraId camera = get_camera();
      OK(ren::MeshId mesh, load_mesh(scene, mesh_path));
      OK(ren::MaterialId material, create_material(scene));
      auto rg = init_random(seed);
      m_create_info.resize(num_entities);
      std::ranges::fill(m_create_info, ren::MeshInstanceCreateInfo{
                                           .mesh = mesh,
                                           .material = material,
                                       });
      m_transforms.resize(num_entities);
      TRY_TO(place_entities(rg, scene, m_transforms));
      TRY_TO(place_light(scene));
      set_camera(scene, camera, num_entities);
      return {};
    }()
                 .transform_error(throw_error)
                 .value();
  }

  auto process_frame(std::chrono::nanoseconds) -> Result<void> override {
    ren::IScene &scene = get_scene();

    if (not m_entities.empty()) {
      scene.destroy_mesh_instances(m_entities);
    }

    m_entities.resize(m_create_info.size());
    TRY_TO(scene.create_mesh_instances(m_create_info, m_entities));
    scene.set_mesh_instance_transforms(m_entities, m_transforms);

    return {};
  }

  [[nodiscard]] static auto run(const char *mesh_path, unsigned num_entities,
                                unsigned seed) -> int {
    return AppBase::run<EntityStressTestApp>(mesh_path, num_entities, seed);
  }

private:
  std::vector<ren::MeshInstanceCreateInfo> m_create_info;
  std::vector<ren::MeshInstanceId> m_entities;
  std::vector<glm::mat4x3> m_transforms;
};

int main(int argc, const char *argv[]) {
  cxxopts::Options options("entity-stress-test",
                           "Draw call stress test for ren");
  // clang-format off
  options.add_options()
    ("f,file", "Path to mesh", cxxopts::value<fs::path>())
    ("n,num-entities", "Number of entities to draw", cxxopts::value<unsigned>()->default_value("10000"))
    ("s,seed", "Random seed", cxxopts::value<unsigned>()->default_value("0"))
    ("h,help", "Show this message");
  // clang-format on
  options.parse_positional({"file"});
  options.positional_help("file");

  cxxopts::ParseResult parse_result = options.parse(argc, argv);
  if (parse_result.count("help") or not parse_result.count("file")) {
    fmt::println("{}", options.help());
    return 0;
  }

  auto mesh_path = parse_result["file"].as<fs::path>();
  auto num_entities = parse_result["num-entities"].as<unsigned>();
  auto seed = parse_result["seed"].as<unsigned>();

  return EntityStressTestApp::run(mesh_path.string().c_str(), num_entities,
                                  seed);
}
