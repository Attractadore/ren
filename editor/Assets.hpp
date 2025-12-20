#pragma once
#include "Guid.hpp"
#include "Meta.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/GenIndex.hpp"
#include "ren/core/Job.hpp"

namespace ren {

struct EditorMesh;

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

void register_all_mesh_content(NotNull<EditorContext*> ctx);
void unregister_all_mesh_content(NotNull<EditorContext*> ctx);

void register_mesh_content(NotNull<EditorContext*> ctx, Guid64 guid);
void unregister_mesh_content(NotNull<EditorContext*> ctx, Guid64 guid);

[[nodiscard]] JobFuture<Result<void, String8>>
job_import_scene(NotNull<EditorContext *> ctx, ArenaTag tag, Path path);

} // namespace ren
