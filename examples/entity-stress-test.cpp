#include "ImGuiApp.hpp"
#include "ren/baking/mesh.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <boost/random.hpp>
#include <boost/random/random_device.hpp>
#include <cmath>
#include <cstdlib>
#include <cxxopts.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>

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
  const aiMesh *ai_mesh = ai_scene->mMeshes[0];
  assert(ai_mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE);
  assert(ai_mesh->HasPositions());
  static_assert(sizeof(glm::vec3) == sizeof(*ai_mesh->mVertices));
  assert(ai_mesh->HasNormals());
  static_assert(sizeof(glm::vec3) == sizeof(*ai_mesh->mNormals));
  static_assert(sizeof(glm::vec4) == sizeof(*ai_mesh->mColors[0]));
  std::vector<unsigned> indices(ai_mesh->mNumFaces * 3);
  for (size_t f = 0; f < ai_mesh->mNumFaces; ++f) {
    assert(ai_mesh->mFaces[f].mNumIndices == 3);
    for (size_t i = 0; i < 3; ++i) {
      indices[f * 3 + i] = ai_mesh->mFaces[f].mIndices[i];
    }
  }

  OK(auto blob,
     ren::bake_mesh_to_memory({
         .num_vertices = ai_mesh->mNumVertices,
         .positions = reinterpret_cast<const glm::vec3 *>(ai_mesh->mVertices),
         .normals = reinterpret_cast<const glm::vec3 *>(ai_mesh->mNormals),
         .colors =
             ai_mesh->HasVertexColors(0)
                 ? reinterpret_cast<const glm::vec4 *>(ai_mesh->mColors[0])
                 : nullptr,
         .num_indices = indices.size(),
         .indices = indices.data(),
     }));
  auto [blob_data, blob_size] = blob;
  OK(ren::MeshId mesh, scene.create_mesh(blob_data, blob_size));
  std::free(blob_data);

  return mesh;
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

auto init_random(unsigned seed) -> boost::random::mt19937 {
  return boost::random::mt19937(seed ? seed : boost::random::random_device()());
}

auto random_transform(boost::random::mt19937 &rg, float min_trans,
                      float max_trans, float min_scale, float max_scale)
    -> glm::mat4x3 {
  boost::random::uniform_real_distribution<float> trans_dist(min_trans,
                                                             max_trans);
  boost::random::uniform_int_distribution<int> axis_dist(INT_MIN, INT_MAX);
  boost::random::uniform_real_distribution<float> angle_dist(
      0.0f, 2.0f * std::numbers::pi);
  boost::random::uniform_real_distribution<float> scale_dist(min_scale,
                                                             max_scale);

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

auto place_entities(boost::random::mt19937 &rg, ren::IScene &scene,
                    ren::MeshId mesh, ren::MaterialId material,
                    unsigned num_entities) -> Result<void> {
  auto [min_trans, max_trans] = get_scene_bounds(num_entities);
  float min_scale = 0.5f;
  float max_scale = 1.0f;

  std::vector<ren::MeshInstanceCreateInfo> create_info(num_entities);
  std::vector<ren::MeshInstanceId> entities(num_entities);
  std::vector<glm::mat4x3> transforms(num_entities);
  for (size_t i = 0; i < num_entities; ++i) {
    create_info[i] = {
        .mesh = mesh,
        .material = material,
    };
    transforms[i] =
        random_transform(rg, min_trans, max_trans, min_scale, max_scale);
  }

  TRY_TO(scene.create_mesh_instances(create_info, entities));
  scene.set_mesh_instance_transforms(entities, transforms);

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
      TRY_TO(place_entities(rg, scene, mesh, material, num_entities));
      TRY_TO(place_light(scene));
      set_camera(scene, camera, num_entities);
      return {};
    }()
                 .transform_error(throw_error)
                 .value();
  }

  [[nodiscard]] static auto run(const char *mesh_path, unsigned num_entities,
                                unsigned seed) -> int {
    return AppBase::run<EntityStressTestApp>(mesh_path, num_entities, seed);
  }
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
