#include "Meta.hpp"
#include "ren/core/Format.hpp"

#include <blake3.h>

namespace ren {

JsonValue to_json(NotNull<Arena *> arena, MetaGltf meta) {
  DynamicArray<JsonKeyValue> json;
  Span<JsonValue> json_meshes =
      Span<JsonValue>::allocate(arena, meta.meshes.m_size);
  for (usize mesh_index : range(meta.meshes.m_size)) {
    MetaMesh meta_mesh = meta.meshes[mesh_index];
    DynamicArray<JsonKeyValue> json_mesh;
    json_mesh.push(arena, {"name", JsonValue::init(arena, meta_mesh.name)});
    json_mesh.push(arena, {"mesh_id", JsonValue::init((i64)meta_mesh.mesh_id)});
    json_mesh.push(arena, {"primitive_id", JsonValue::init((i64)meta_mesh.primitive_id)});
    json_mesh.push(arena,
                   {"guid", JsonValue::init(to_string(arena, meta_mesh.guid))});
    json_meshes[mesh_index] = JsonValue::init(json_mesh);
  }
  json.push(arena, {"meshes", JsonValue::init(json_meshes)});
  return JsonValue::init(json);
}

Result<MetaGltf, MetaGltfErrorInfo> meta_gltf_from_json(NotNull<Arena *> arena,
                                                        JsonValue json) {
  if (json.type != JsonType::Object) {
    return MetaGltfErrorInfo{};
  }

  JsonValue json_meshes = json_value(json, "meshes");
  if (json_meshes.type != JsonType::Array) {
    return MetaGltfErrorInfo{};
  }
  auto meta_meshes =
      Span<MetaMesh>::allocate(arena, json_array(json_meshes).m_size);

  for (usize mesh_index : range(json_array(json_meshes).m_size)) {
    JsonValue json_mesh = json_array(json_meshes)[mesh_index];
    if (json_mesh.type != JsonType::Object) {
      return MetaGltfErrorInfo{};
    }

    JsonValue json_name = json_value(json_mesh, "name");
    if (json_name.type != JsonType::String) {
      return MetaGltfErrorInfo{};
    }

    JsonValue json_mesh_id = json_value(json_mesh, "mesh_id");
    if (json_mesh_id.type != JsonType::Integer) {
      return MetaGltfErrorInfo{};
    }

    JsonValue json_primitive_id = json_value(json_mesh, "primitive_id");
    if (json_primitive_id.type != JsonType::Integer) {
      return MetaGltfErrorInfo{};
    }

    JsonValue json_guid = json_value(json_mesh, "guid");
    if (json_guid.type != JsonType::String) {
      return MetaGltfErrorInfo{};
    }
    Optional<Guid64> guid = guid64_from_string(json_string(json_guid));
    if (!guid) {
      return MetaGltfErrorInfo{};
    }

    meta_meshes[mesh_index] = {
        .name = json_string(json_name),
        .mesh_id = (u32)json_integer(json_mesh_id),
        .primitive_id = (u32)json_integer(json_primitive_id),
        .guid = *guid,
    };
  }

  return MetaGltf{
      .meshes = meta_meshes,
  };
}

String8 to_string(NotNull<Arena *> arena, MetaGltfErrorInfo error) {
  return "Unknown error";
}

MetaGltf meta_gltf_generate(NotNull<Arena *> arena, Gltf gltf,
                            Path filename) {
  ScratchArena scratch;
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);

  Path stem = filename.stem();

  auto meta_meshes = Span<MetaMesh>::allocate(arena, gltf.meshes.m_size);
  usize meta_mesh_offset = 0;

  for (usize gltf_mesh_index : range(gltf.meshes.m_size)) {
    String8 gltf_mesh_name = 
        gltf.meshes[gltf_mesh_index].name;
    for (usize gltf_primitive_index :
         range(gltf.meshes[gltf_mesh_index].primitives.m_size)) {
      String8 gltf_primitive_name = format(scratch, "{}", gltf_primitive_index);
      blake3_hasher_reset(&hasher);
      String8 guid_src = String8::join(
          arena, {stem.m_str, gltf_mesh_name, gltf_primitive_name}, "::");
      blake3_hasher_update(&hasher, guid_src.m_str, guid_src.m_size);
      Guid64 guid;
      blake3_hasher_finalize(&hasher, guid.m_data, sizeof(guid));
      meta_meshes[meta_mesh_offset++] = {
          .name = guid_src,
          .mesh_id = (u32)gltf_mesh_index,
          .primitive_id = (u32)gltf_primitive_index,
          .guid = guid,
      };
    }
  }

  return {
      .meshes = meta_meshes,
  };
}

} // namespace ren
