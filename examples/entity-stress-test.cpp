#include "ImGuiApp.hpp"
#include "ren/baking/mesh.hpp"
#include "ren/core/CmdLine.hpp"
#include "ren/core/Format.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cmath>
#include <cstdlib>
#include <fmt/format.h>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>
#include <random>

namespace {

ren::Handle<ren::Mesh> load_mesh(ren::NotNull<ren::Arena *> frame_arena,
                                 ren::NotNull<ren::Scene *> scene,
                                 ren::String8 path) {
  ren::ScratchArena scratch(frame_arena);
  Assimp::Importer importer;
  importer.SetPropertyBool(AI_CONFIG_PP_PTV_NORMALIZE, true);
  const aiScene *ai_scene = importer.ReadFile(
      path.zero_terminated(scratch),
      // clang-format off
      aiProcess_Triangulate |
      aiProcess_GenNormals |
      aiProcess_PreTransformVertices |
      aiProcess_SortByPType |
      aiProcess_FindInvalidData
      // clang-format on
  );
  if (!ai_scene) {
    fmt::println(stderr, "{}", importer.GetErrorString());
    return ren::NullHandle;
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

  ren::Blob blob = ren::bake_mesh_to_memory(
      scratch,
      {
          .num_vertices = ai_mesh->mNumVertices,
          .positions = reinterpret_cast<const glm::vec3 *>(ai_mesh->mVertices),
          .normals = reinterpret_cast<const glm::vec3 *>(ai_mesh->mNormals),
          .colors =
              ai_mesh->HasVertexColors(0)
                  ? reinterpret_cast<const glm::vec4 *>(ai_mesh->mColors[0])
                  : nullptr,
          .indices = {indices.data(), indices.size()},
      });
  return ren::create_mesh(frame_arena, scene, blob.data, blob.size);
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

void place_entities(
    ren::NotNull<ren::Arena *> arena, ren::NotNull<ren::Arena *> frame_arena,
    std::mt19937 &rg, ren::Scene *scene, ren::Handle<ren::Mesh> mesh,
    ren::Handle<ren::Material> material, unsigned num_entities,
    ren::NotNull<ren::Handle<ren::MeshInstance> **> out_entities,
    ren::NotNull<glm::mat4x3 **> out_transforms) {
  auto [min_trans, max_trans] = get_scene_bounds(num_entities);
  float min_scale = 0.5f;
  float max_scale = 1.0f;

  ren::ScratchArena scratch({arena, frame_arena});

  auto *create_info =
      scratch->allocate<ren::MeshInstanceCreateInfo>(num_entities);
  auto *entities =
      arena->allocate<ren::Handle<ren::MeshInstance>>(num_entities);
  auto *transforms = arena->allocate<glm::mat4x3>(num_entities);
  for (size_t i = 0; i < num_entities; ++i) {
    create_info[i] = {
        .mesh = mesh,
        .material = material,
    };
    transforms[i] =
        random_transform(rg, min_trans, max_trans, min_scale, max_scale);
  }
  ren::create_mesh_instances(frame_arena, scene, {create_info, num_entities},
                             {entities, num_entities});
  *out_entities = entities;
  *out_transforms = transforms;
}

void place_light(ren::Scene *scene) {
  create_directional_light(scene, {
                                      .origin = {-1.0f, 0.0f, 1.0f},
                                  });
}

void set_camera(ren::Scene *scene, ren::Handle<ren::Camera> camera,
                unsigned num_entities) {
  auto [scene_min, _] = get_scene_bounds(num_entities);

  set_camera_perspective_projection(scene, camera, {});
  set_camera_transform(scene, camera,
                       {
                           .position = {scene_min, 0.0f, 0.0f},
                           .forward = {1.0f, 0.0f, 0.0f},
                           .up = {0.0f, 0.0f, 1.0f},
                       });
}

} // namespace

class EntityStressTestApp : public ImGuiApp {
  ren::usize m_num_entities = 0;
  ren::Handle<ren::MeshInstance> *m_entities = nullptr;
  glm::mat4x3 *m_transforms = nullptr;

public:
  void init(ren::String8 mesh_path, unsigned num_entities, ren::u64 seed) {
    ren::ScratchArena scratch;
    ImGuiApp::init(format(scratch, "Entity Stress Test: {} @ {}", mesh_path,
                          num_entities));
    m_num_entities = num_entities;
    ren::Scene *scene = get_scene();
    ren::Handle<ren::Camera> camera = get_camera();
    ren::Handle<ren::Mesh> mesh = load_mesh(&m_frame_arena, scene, mesh_path);
    ren::Handle<ren::Material> material =
        ren::create_material(&m_frame_arena, scene, {.metallic_factor = 0.0f});
    auto rg = init_random(seed);
    place_entities(&m_arena, &m_frame_arena, rg, scene, mesh, material,
                   m_num_entities, &m_entities, &m_transforms);
    place_light(scene);
    set_camera(scene, camera, num_entities);
  }

  void process_frame(ren::u64 dt_ns) override {
    ren::set_mesh_instance_transforms(&m_frame_arena, get_scene(),
                                      {m_entities, m_num_entities},
                                      {m_transforms, m_num_entities});
  }

  static void run(ren::String8 mesh_path, unsigned num_entities,
                  unsigned seed) {
    AppBase::run<EntityStressTestApp>(mesh_path, num_entities, seed);
  }
};

enum EntityStressTestOptions {
  OPTION_FILE,
  OPTION_NUM_ENTITIES,
  OPTION_SEED,
  OPTION_HELP,
  OPTION_COUNT,
};

int main(int argc, const char *argv[]) {
  ren::ScratchArena::init_allocator();

  // clang-format off
  ren::CmdLineOption options[] = {
    {OPTION_FILE, ren::CmdLineString, "file", 'f', "Path to mesh", ren::CmdLinePositional},
    {OPTION_NUM_ENTITIES, ren::CmdLineUInt, "num-entities", 'n', "Number of entities to draw"},
    {OPTION_SEED, ren::CmdLineUInt, "seed", 's', "Random seed"},
    {OPTION_HELP, ren::CmdLineFlag, "help", 'h', "Show this message"},
  };
  // clang-format on
  ren::ParsedCmdLineOption parsed[OPTION_COUNT];
  bool success = ren::parse_cmd_line(argv, options, parsed);
  if (!success or parsed[OPTION_HELP].is_set) {
    ren::ScratchArena scratch;
    fmt::print("{}", ren::cmd_line_help(scratch, argv[0], options));
    return EXIT_FAILURE;
  }

  ren::String8 mesh_path = parsed[OPTION_FILE].as_string;
  ren::u32 num_entities = 100'000;
  if (parsed[OPTION_NUM_ENTITIES].is_set) {
    num_entities = parsed[OPTION_NUM_ENTITIES].as_uint;
  }
  ren::u64 seed = 0;
  if (parsed[OPTION_SEED].is_set) {
    seed = parsed[OPTION_SEED].as_uint;
  }

  EntityStressTestApp::run(mesh_path, num_entities, seed);
}
