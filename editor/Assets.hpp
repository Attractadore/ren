#pragma once
#include "Guid.hpp"
#include "Meta.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/GenIndex.hpp"
#include "ren/core/Job.hpp"

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>

namespace ren {

struct EditorMesh;
struct Mesh;

struct EditorGltfScene {
  Path bin_filename;
  Path gltf_filename;
  Path meta_filename;
  Handle<EditorMesh> first_mesh;
};

struct EditorMesh {
  Guid64 guid = {};
  String8 name;
  Handle<EditorMesh> next;
  Handle<Mesh> gfx_handle;
  bool is_dirty : 1 = false;
};

struct EditorContext;

void register_all_assets(NotNull<EditorContext *> ctx);
void unregister_all_assets(NotNull<EditorContext *> ctx);

void register_gltf_scene(NotNull<EditorContext *> ctx, const MetaGltf &meta,
                         Path meta_filename);
void register_gltf_scene(NotNull<EditorContext *> ctx, Path meta_path);
void unregister_gltf_scene(NotNull<EditorContext *> ctx, Path meta_filename);

void register_all_gltf_scenes(NotNull<EditorContext *> ctx);
void unregister_all_gltf_scenes(NotNull<EditorContext *> ctx);

void register_all_content(NotNull<EditorContext *> ctx);
void unregister_all_content(NotNull<EditorContext *> ctx);

void register_all_mesh_content(NotNull<EditorContext *> ctx);
void unregister_all_mesh_content(NotNull<EditorContext *> ctx);

void register_mesh_content(NotNull<EditorContext *> ctx, Guid64 guid);
void unregister_mesh_content(NotNull<EditorContext *> ctx, Guid64 guid);

[[nodiscard]] JobFuture<Result<void, String8>>
job_import_scene(NotNull<EditorContext *> ctx, ArenaTag tag, Path path);

struct EditorSceneNode {
  Guid64 guid;
  String8 name;
  Handle<EditorSceneNode> parent;
  Handle<EditorSceneNode> first_child;
  Handle<EditorSceneNode> last_child;
  Handle<EditorSceneNode> prev_sibling;
  Handle<EditorSceneNode> next_sibling;
  glm::vec3 translation = {0.0f, 0.0f, 0.0f};
  glm::quat rotation = glm::identity<glm::quat>();
  glm::vec3 scale = {1.0f, 1.0f, 1.0f};
};

Handle<EditorSceneNode> add_scene_root_node(NotNull<EditorContext *> ctx);

/// Add a new scene node.
/// If insert_after is null, insert in the beginning.
Handle<EditorSceneNode> add_scene_node(NotNull<EditorContext *> ctx,
                                       Handle<EditorSceneNode> parent,
                                       Handle<EditorSceneNode> insert_after,
                                       String8 name);

void remove_scene_node(NotNull<EditorContext *> ctx,
                       Handle<EditorSceneNode> node);

void remove_scene_node_with_children(NotNull<EditorContext *> ctx,
                                     Handle<EditorSceneNode> node);

Guid64 generate_guid(NotNull<EditorContext *> ctx);

} // namespace ren
