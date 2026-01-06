#include "ImGuiApp.hpp"
#include "ren/baking/mesh.hpp"
#include "ren/core/CmdLine.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/GLTF.hpp"
#include "ren/core/Job.hpp"
#include "ren/core/sh/Random.h"

#include <cmath>
#include <cstdlib>
#include <fmt/base.h>
#include <glm/gtc/matrix_transform.hpp>

namespace {

using namespace ren;

struct DemoScene {
  Span<glm::mat4x3> transforms;
  Span<Handle<Mesh>> meshes;
};

DemoScene load_scene(ren::NotNull<ren::Arena *> frame_arena,
                     ren::NotNull<ren::Scene *> scene, ren::Path path) {
  ren::ScratchArena scratch;

  ren::Result<ren::Gltf, ren::GltfErrorInfo> gltf =
      ren::load_gltf_with_blobs(scratch, path);
  if (!gltf) {
    fmt::println(stderr, "{}", gltf.error().message);
    exit(EXIT_FAILURE);
  }
  // clang-format off
  ren::gltf_optimize(scratch, &*gltf,
      ren::GltfOptimize::RemoveCameras |
      ren::GltfOptimize::RemoveMaterials |
      ren::GltfOptimize::RemoveImages |
      ren::GltfOptimize::RemoveSkins |
      ren::GltfOptimize::RemoveAnimations |
      ren::GltfOptimize::RemoveRedundantMeshes |
      ren::GltfOptimize::ConvertMeshAccessors |
      ren::GltfOptimize::CollapseSceneHierarchy |
      ren::GltfOptimize::RemoveRedundantNodes |
      ren::GltfOptimize::RemoveEmptyScenes | 
      ren::GltfOptimize::NormalizeSceneBounds
  );
  // clang-format on
  if (gltf->meshes.is_empty()) {
    fmt::println(stderr, "Scene doesn't contain any (triangle) meshes");
    exit(EXIT_FAILURE);
  }

  Span<usize> primitive_offsets =
      Span<usize>::allocate(scratch, gltf->meshes.size());
  usize num_primitives = 0;
  for (usize mesh_index : range(gltf->meshes.size())) {
    const GltfMesh &mesh = gltf->meshes[mesh_index];
    primitive_offsets[mesh_index] = num_primitives;
    num_primitives += mesh.primitives.size();
  }

  Span<Handle<Mesh>> primitive_handles =
      Span<Handle<Mesh>>::allocate(scratch, num_primitives);
  for (usize mesh_index : range(gltf->meshes.size())) {
    const GltfMesh &mesh = gltf->meshes[mesh_index];
    for (usize primitive_index : range(mesh.primitives.size())) {
      ren::MeshInfo mesh_info = ren::gltf_primitive_to_mesh_info(
          gltf->blobs[0], *gltf, mesh.primitives[primitive_index]);
      ren::Blob blob = ren::bake_mesh_to_memory(scratch, mesh_info);
      primitive_handles[primitive_offsets[mesh_index] + primitive_index] =
          ren::create_mesh(frame_arena, scene, blob.data, blob.size);
    }
  }

  DynamicArray<glm::mat4x3> scene_transforms;
  DynamicArray<Handle<Mesh>> scene_meshes;
  for (i32 node_index : gltf->scenes[0].nodes) {
    const GltfNode &node = gltf->nodes[node_index];
    if (node.mesh == -1) {
      continue;
    }
    if (glm::transpose(node.matrix)[3] != glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)) {
      continue;
    }
    const GltfMesh &mesh = gltf->meshes[node.mesh];
    for (usize primitive_index : range(mesh.primitives.size())) {
      scene_transforms.push(scratch, node.matrix);
      scene_meshes.push(
          scratch,
          primitive_handles[primitive_offsets[node.mesh] + primitive_index]);
    }
  }

  return {
      .transforms = Span(scene_transforms).copy(frame_arena),
      .meshes = Span(scene_meshes).copy(frame_arena),
  };
}

glm::vec2 get_scene_bounds(unsigned num_entities) {
  float s = std::cbrt(num_entities);
  return {-s, s};
}

glm::vec3 uniform_sample_sphere(glm::vec2 Xi) {
  float phi = Xi.x * 6.2832f;
  float z = 2.0f * Xi.y - 1.0f;
  float r = glm::sqrt(1.0f - z * z);
  return glm::vec3(r * cos(phi), r * sin(phi), z);
}

auto random_transform(float i, float min_trans, float max_trans,
                      float min_scale, float max_scale) -> glm::mat4x3 {
  glm::vec3 translate =
      glm::mix(glm::vec3(min_trans), glm::vec3(max_trans), ren::sh::r3_seq(i));

  glm::vec3 axis = uniform_sample_sphere(ren::sh::r2_seq(i));
  float angle = 2.0f * glm::pi<float>() * ren::sh::r1_seq(i);

  glm::vec3 scale =
      glm::mix(glm::vec3(min_scale), glm::vec3(max_scale), ren::sh::r3_seq(i));

  glm::mat4 transform(1.0f);
  transform = glm::translate(transform, translate);
  transform = glm::rotate(transform, angle, axis);
  transform = glm::scale(transform, scale);

  return transform;
}

void place_entities(
    ren::NotNull<ren::Arena *> arena, ren::NotNull<ren::Arena *> frame_arena,
    ren::Scene *scene, DemoScene demo_scene,
    ren::Handle<ren::Material> material, unsigned num_entities,
    ren::NotNull<ren::Handle<ren::MeshInstance> **> out_entities,
    ren::NotNull<glm::mat4x3 **> out_transforms) {

  glm::vec2 bounds = get_scene_bounds(num_entities);
  float min_scale = 0.5f;
  float max_scale = 1.0f;

  ren::ScratchArena scratch;

  usize scene_size = demo_scene.transforms.size();

  auto *create_info =
      scratch->allocate<ren::MeshInstanceCreateInfo>(num_entities * scene_size);
  auto *entities = arena->allocate<ren::Handle<ren::MeshInstance>>(
      num_entities * scene_size);
  auto *transforms = arena->allocate<glm::mat4x3>(num_entities * scene_size);
  for (size_t i = 0; i < num_entities; ++i) {
    glm::mat4x3 scene_transform =
        random_transform(i, bounds[0], bounds[1], min_scale, max_scale);
    for (usize scene_index : range(scene_size)) {
      create_info[i * scene_size + scene_index] = {
          .mesh = demo_scene.meshes[scene_index], .material = material};
      transforms[i * scene_size + scene_index] =
          scene_transform * glm::mat4(demo_scene.transforms[scene_index]);
    }
  }
  ren::create_mesh_instances(frame_arena, scene,
                             {create_info, num_entities * scene_size},
                             {entities, num_entities * scene_size});
  *out_entities = entities;
  *out_transforms = transforms;
}

void place_light(ren::Scene *scene) {
  std::ignore =
      create_directional_light(scene, {
                                          .origin = {-1.0f, 0.0f, 1.0f},
                                      });
}

void set_camera(ren::Scene *scene, ren::Handle<ren::Camera> camera,
                unsigned num_entities) {
  glm::vec2 bounds = get_scene_bounds(num_entities);

  set_camera_perspective_projection(scene, camera, {});
  set_camera_transform(scene, camera,
                       {
                           .position = {bounds[0], 0.0f, 0.0f},
                           .forward = {1.0f, 0.0f, 0.0f},
                           .up = {0.0f, 0.0f, 1.0f},
                       });
}

} // namespace

class EntityStressTestApp : public ImGuiApp {
  ren::usize m_num_entities = 0;
  usize m_scene_size = 0;
  ren::Handle<ren::MeshInstance> *m_entities = nullptr;
  glm::mat4x3 *m_transforms = nullptr;

public:
  void init(ren::Path mesh_path, unsigned num_entities) {
    ren::ScratchArena scratch;
    ImGuiApp::init(format(scratch, "Entity Stress Test: {} @ {}", mesh_path,
                          num_entities));
    m_num_entities = num_entities;
    ren::Scene *scene = get_scene();
    ren::Handle<ren::Camera> camera = get_camera();
    DemoScene demo_scene = load_scene(&m_frame_arena, scene, mesh_path);
    m_scene_size = demo_scene.transforms.size();
    ren::Handle<ren::Material> material =
        ren::create_material(&m_frame_arena, scene, {.metallic_factor = 0.0f});
    place_entities(&m_arena, &m_frame_arena, scene, demo_scene, material,
                   m_num_entities, &m_entities, &m_transforms);
    place_light(scene);
    set_camera(scene, camera, num_entities);
  }

  void process_frame(ren::u64 dt_ns) override {
    ren::set_mesh_instance_transforms(
        &m_frame_arena, get_scene(),
        {m_entities, m_num_entities * m_scene_size},
        {m_transforms, m_num_entities * m_scene_size});
  }

  static void run(ren::Path mesh_path, unsigned num_entities) {
    AppBase::run<EntityStressTestApp>(mesh_path, num_entities);
  }
};

enum EntityStressTestOptions {
  OPTION_FILE,
  OPTION_NUM_ENTITIES,
  OPTION_HELP,
  OPTION_COUNT,
};

int main(int argc, const char *argv[]) {
  ren::ScratchArena::init_for_thread();
  ren::launch_job_server();
  ren::ScratchArena scratch;

  // clang-format off
  ren::CmdLineOption options[] = {
    {OPTION_FILE, ren::CmdLinePath, "file", 'f', "Path to mesh", ren::CmdLinePositional},
    {OPTION_NUM_ENTITIES, ren::CmdLineUInt, "num-entities", 'n', "Number of entities to draw"},
    {OPTION_HELP, ren::CmdLineFlag, "help", 'h', "Show this message"},
  };
  // clang-format on
  ren::ParsedCmdLineOption parsed[OPTION_COUNT];
  bool success = ren::parse_cmd_line(scratch, argv, options, parsed);
  if (!success or parsed[OPTION_HELP].is_set) {
    ren::ScratchArena scratch;
    fmt::print("{}", ren::cmd_line_help(scratch, argv[0], options));
    return EXIT_FAILURE;
  }

  ren::Path mesh_path = parsed[OPTION_FILE].as_path;
  ren::u32 num_entities = 100'000;
  if (parsed[OPTION_NUM_ENTITIES].is_set) {
    num_entities = parsed[OPTION_NUM_ENTITIES].as_uint;
  }

  EntityStressTestApp::run(mesh_path, num_entities);
}
