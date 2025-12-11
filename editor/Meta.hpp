#pragma once
#include "Guid.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/JSON.hpp"
#include "ren/core/Optional.hpp"
#include "ren/core/StdDef.hpp"
#include "ren/core/String.hpp"

namespace ren {

struct MetaMesh {
  String8 name;
  u32 mesh_id = 0;
  u32 primitive_id = 0;
  Guid64 guid;
};

struct MetaGltf {
  String8 src;
  Span<const MetaMesh> meshes;
};

JsonValue to_json(NotNull<Arena *> arena, MetaGltf scene);

struct MetaGltfErrorInfo {};

String8 to_string(NotNull<Arena *> arena, MetaGltfErrorInfo error);

Result<MetaGltf, MetaGltfErrorInfo> meta_gltf_from_json(NotNull<Arena *> arena,
                                                        JsonValue value);

MetaGltf meta_gltf_generate(NotNull<Arena *> arena, JsonValue gltf,
                            Path filename);

} // namespace ren
