#include "ren/core/glTF.hpp"
#include "ren/core/JSON.hpp"
#include "ren/core/Optional.hpp"

namespace ren {
bool try_get_json_string(JsonValue json, String8 key, String8 &out) {
  JsonValue val = json_value(json, key);
  if (val.type == JsonType::String) {
    out = json_string(val);
    return true;
  }
  return false;
}

i32 json_get_int(JsonValue json, String8 key, i32 default_val) {
  JsonValue val = json_value(json, key);
  if (val.type == JsonType::Integer) {
    return (i32)json_integer(val);
  }
  return default_val;
}

float json_get_float(JsonValue json, String8 key, float default_val) {
  JsonValue val = json_value(json, key);
  if (val.type == JsonType::Number) {
    return (float)val.number;
  } else if (val.type == JsonType::Integer) {
    return (float)val.integer;
  }
  return default_val;
}

bool try_get_json_string_required(JsonValue json, String8 key, String8 &out) {
  JsonValue val = json_value(json, key);
  if (val.type != JsonType::Null) {
    out = json_string(val);
    return true;
  }
  return false;
}

bool json_get_bool(JsonValue json, String8 key, bool default_val) {
  JsonValue val = json_value(json, key);
  if (val.type == JsonType::Boolean) {
    return val.boolean;
  }
  return default_val;
}

Optional<Span<float>> json_get_float_array(NotNull<Arena *> arena,
                                           JsonValue json, String8 key) {
  ScratchArena scratch;

  JsonValue val = json_value(json, key);
  if (val.type != JsonType::Array) {
    return {};
  }

  Span<const JsonValue> arr = json_array(val);
  Span<float> out = Span<float>::allocate(scratch, arr.m_size);

  for (i32 i = 0; i < out.m_size; i++) {
    if (arr[i].type == JsonType::Number) {
      out[i] = (float)arr[i].number;
    } else if (arr[i].type == JsonType::Integer) {
      out[i] = (float)arr[i].integer;
    } else {
      return {};
    }
  }

  return out.copy(arena);
}

static Result<GltfAsset, GltfErrorInfo> parse_asset(JsonValue json) {
  if (json.type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Expected object type."};
  }

  GltfAsset asset;

  if (!try_get_json_string(json, "version", asset.version)) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Expected filed \"version\" not found."};
  }

  try_get_json_string(json, "generator", asset.generator);
  try_get_json_string(json, "copyright", asset.copyright);
  try_get_json_string(json, "minVersion", asset.min_version);

  return asset;
}

static Result<GltfScene, GltfErrorInfo> parse_scene(NotNull<Arena *> arena, JsonValue json) {
  GltfScene scene = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Expected object."};
  }

  try_get_json_string(json, "name", scene.name);

  JsonValue nodes = json_value(json, "nodes");
  if (nodes.type == JsonType::Array) {
    Span<const JsonValue> node_arr = json_array(nodes);
    scene.nodes = DynamicArray<i32>::init(arena, node_arr.m_size);

    for (const JsonValue &node : node_arr) {
      if (node.type == JsonType::Integer) {
        scene.nodes.push(arena, (i32)node.integer);
      }
    }
  }

  return scene;
}

static Result<GltfNode, GltfErrorInfo> parse_node(NotNull<Arena *> arena, JsonValue json) {
  GltfNode node = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Expected object."};
  }

  try_get_json_string(json, "name", node.name);
  node.camera = json_get_int(json, "camera", -1);
  node.mesh = json_get_int(json, "mesh", -1);
  node.skin = json_get_int(json, "skin", -1);

  JsonValue matrix_val = json_value(json, "matrix");
  if (matrix_val && matrix_val.type != JsonType::Array) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "\"matrix\" should be array."};
  }

  if (matrix_val) {
    if (Optional<Span<float>> res =
            json_get_float_array(arena, json, "matrix")) {
      memcpy(&node.matrix[0], (*res).m_data, sizeof(node.matrix));
    }
  } else {
    if (Optional<Span<float>> res =
            json_get_float_array(arena, json, "translation")) {
      memcpy(&node.translation[0], (*res).m_data, sizeof(node.translation));
    }
    if (Optional<Span<float>> res =
            json_get_float_array(arena, json, "rotation")) {
      memcpy(&node.rotation[0], (*res).m_data, sizeof(node.rotation));
    }
    if (Optional<Span<float>> res =
            json_get_float_array(arena, json, "scale")) {
      memcpy(&node.scale[0], (*res).m_data, sizeof(node.scale));
    }
  }

  JsonValue children = json_value(json, "children");
  if (children.type == JsonType::Array) {
    Span<const JsonValue> child_arr = json_array(children);
    node.children = DynamicArray<i32>::init(arena, child_arr.m_size);

    for (const JsonValue &child : child_arr) {
      if (child.type == JsonType::Integer) {
        node.children.push(arena, (i32)child.integer);
      }
    }
  }

  return node;
}

static Result<GltfPrimitive, GltfErrorInfo> parse_primitive(NotNull<Arena *> arena, JsonValue json) {
  GltfPrimitive prim = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Expected object."};
  }

  prim.indices = json_get_int(json, "indices", -1);
  prim.material = json_get_int(json, "material", -1);
  prim.mode = (GltfTopology)json_get_int(json, "mode", GLTF_TOPOLOGY_TRIANGLES);

  JsonValue attrs = json_value(json, "attributes");
  if (attrs.type == JsonType::Object) {
    prim.attributes = DynamicArray<GltfAttribute>::init(arena, 0);
    Span<const JsonKeyValue> obj = json_object(attrs);
    for (const JsonKeyValue &kv : obj) {
      if (kv.value.type == JsonType::Integer) {
        GltfAttribute attr;
        attr.name = kv.key;
        attr.accessor = (i32)kv.value.integer;
        prim.attributes.push(arena, attr);
      }
    }
  }

  return prim;
}

static Result<GltfMesh, GltfErrorInfo> parse_mesh(NotNull<Arena *> arena, JsonValue json) {
  GltfMesh mesh = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Expected object."};
  }

  try_get_json_string(json, "name", mesh.name);

  JsonValue prims = json_value(json, "primitives");
  if (prims.type == JsonType::Array) {
    Span<const JsonValue> prim_arr = json_array(prims);
    mesh.primitives = DynamicArray<GltfPrimitive>::init(arena, prim_arr.m_size);

    for (const JsonValue &prim_json : prim_arr) {
      Result<GltfPrimitive, GltfErrorInfo> parse_result =
          parse_primitive(arena, prim_json);
      if (!parse_result) {
        return parse_result.error();
      }
      mesh.primitives.push(arena, *parse_result);
    }
  }

  return mesh;
}

static Result<GltfImage, GltfErrorInfo> parse_image(NotNull<Arena *> arena, JsonValue json) {
  GltfImage image = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Expected object."};
  }

  try_get_json_string(json, "name", image.name);
  image.buffer_view = json_get_int(json, "bufferView", -1);
  try_get_json_string(json, "mimeType", image.mime_type);
  try_get_json_string(json, "uri", image.uri);

  return image;
}

static Result<GltfAccessor, GltfErrorInfo> parse_accessor(NotNull<Arena *> arena, JsonValue json) {
  GltfAccessor accessor = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Expected object."};
  }

  try_get_json_string(json, "name", accessor.name);
  accessor.buffer_view = json_get_int(json, "bufferView", -1);
  accessor.buffer_offset = json_get_int(json, "byteOffset", 0);
  accessor.component_type =
      (GltfComponentType)json_get_int(json, "componentType", 0);
  accessor.normalized = json_get_bool(json, "normalized", false);
  accessor.count = json_get_int(json, "count", 0);

  String8 type_str = json_string_value_or(json, "type", "SCALAR");
  if (type_str == "SCALAR") {
    accessor.type = GLTF_TYPE_SCALAR;
  } else if (type_str == "VEC2") {
    accessor.type = GLTF_TYPE_VEC2;
  } else if (type_str == "VEC3") {
    accessor.type = GLTF_TYPE_VEC3;
  } else if (type_str == "VEC4") {
    accessor.type = GLTF_TYPE_VEC4;
  } else if (type_str == "MAT2") {
    accessor.type = GLTF_TYPE_MAT2;
  } else if (type_str == "MAT3") {
    accessor.type = GLTF_TYPE_MAT3;
  } else if (type_str == "MAT4") {
    accessor.type = GLTF_TYPE_MAT4;
  }

  JsonValue min_arr = json_value(json, "min");
  if (min_arr.type == JsonType::Array) {
    Span<const JsonValue> min_values = json_array(min_arr);
    ren_assert(min_values.m_size < 16 && "Min values overflow.");
    for (i32 i = 0; i < min_values.m_size; i++) {
      if (min_values[i].type == JsonType::Number) {
        accessor.min[i] = (float)min_values[i].number;
      } else if (min_values[i].type == JsonType::Integer) {
        accessor.min[i] = (float)min_values[i].integer;
      }
    }
  }

  JsonValue max_arr = json_value(json, "max");
  if (max_arr.type == JsonType::Array) {
    Span<const JsonValue> max_values = json_array(max_arr);
    ren_assert(max_values.m_size < 16 && "Max values overflow.");
    for (i32 i = 0; i < max_values.m_size; i++) {
      if (max_values[i].type == JsonType::Number) {
        accessor.max[i] = (float)max_values[i].number;
      } else if (max_values[i].type == JsonType::Integer) {
        accessor.max[i] = (float)max_values[i].integer;
      }
    }
  }

  return accessor;
}

static Result<GltfBufferView, GltfErrorInfo> parse_buffer_view(NotNull<Arena *> arena,
                                        JsonValue json) {
  GltfBufferView view = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Expected object."};
  }

  try_get_json_string(json, "name", view.name);
  view.buffer = json_get_int(json, "buffer", 0);
  view.byte_offset = json_get_int(json, "byteOffset", 0);
  view.byte_length = json_get_int(json, "byteLength", 0);
  view.byte_stride = json_get_int(json, "byteStride", 0);
  view.target = (GltfBufferTarget)json_get_int(json, "target", 0);

  return view;
}

static Result<GltfBuffer, GltfErrorInfo> parse_buffer(NotNull<Arena *> arena, JsonValue json) {
  GltfBuffer buffer = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Expected object."};
  }

  try_get_json_string(json, "name", buffer.name);
  try_get_json_string(json, "uri", buffer.uri);

  return buffer;
}

template <typename T>
static Result<DynamicArray<T>, GltfErrorInfo> parse_array(
    NotNull<Arena *> arena, JsonValue arr,
    Result<T, GltfErrorInfo> (*parse_func)(NotNull<Arena *>, JsonValue)) {
  if (arr.type != JsonType::Array) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Expected array."};
  }

  Span<const JsonValue> values = json_array(arr);
  DynamicArray<T> out = DynamicArray<T>::init(arena, values.m_size);

  for (const JsonValue &val : values) {
    Result<T, GltfErrorInfo> parse_result = parse_func(arena, val);
    if (!parse_result) {
      return parse_result.error();
    }
    out.push(arena, *parse_result);
  }

  return out;
}

Result<Gltf, GltfErrorInfo> gltf_parse(NotNull<Arena *> arena, Span<u8> buffer,
                                   Path base_path) {
  Result<JsonValue, JsonErrorInfo> json =
      json_parse(arena, String8((const char *)buffer.m_data, buffer.m_size));
  if (!json) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Invalid json format."};
  }

  if (json->type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Expected object type."};
  }

  Gltf gltf{};

  Result<GltfAsset, GltfErrorInfo> asset_parse_result = parse_asset(json_value(*json, "asset"));
  if (!asset_parse_result) {
    return asset_parse_result.error();
  }
  gltf.asset = *asset_parse_result;

  gltf.scene = json_get_int(*json, "scene", -1);

  JsonValue arr;
  arr = json_value(*json, "scenes");
  if (arr) {
    Result<DynamicArray<GltfScene>, GltfErrorInfo> parse_result =
        parse_array(arena, arr, parse_scene);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.scenes = *parse_result;
  }
  arr = json_value(*json, "nodes");
  if (arr) {
    Result<DynamicArray<GltfNode>, GltfErrorInfo> parse_result =
        parse_array(arena, arr, parse_node);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.nodes = *parse_result;
  }
  arr = json_value(*json, "meshes");
  if (arr) {
    Result<DynamicArray<GltfMesh>, GltfErrorInfo> parse_result =
        parse_array(arena, arr, parse_mesh);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.meshes = *parse_result;
  }
  arr = json_value(*json, "images");
  if (arr) {
    Result<DynamicArray<GltfImage>, GltfErrorInfo> parse_result =
        parse_array(arena, arr, parse_image);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.images = *parse_result;
  }
  arr = json_value(*json, "accessors");
  if (arr) {
    Result<DynamicArray<GltfAccessor>, GltfErrorInfo> parse_result =
        parse_array(arena, arr, parse_accessor);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.accessors = *parse_result;
  }
  arr = json_value(*json, "bufferViews");
  if (arr) {
    Result<DynamicArray<GltfBufferView>, GltfErrorInfo> parse_result =
        parse_array(arena, arr, parse_buffer_view);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.buffer_views = *parse_result;
  }
  arr = json_value(*json, "buffers");
  if (arr) {
    Result<DynamicArray<GltfBuffer>, GltfErrorInfo> parse_result =
        parse_array(arena, arr, parse_buffer);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.buffers = *parse_result;

    for (GltfBuffer &buffer : gltf.buffers) {
      if (buffer.data.m_size || !buffer.uri) {
        continue;
      }

      Path buffer_path = base_path.concat(arena, Path::init(buffer.uri));

      IoResult<Span<u8>> file_data = read<u8>(arena, buffer_path);
      if (!file_data) {
        return GltfErrorInfo{.error = GltfError::IO,
                             .desc = "Failed reading buffer."};
      }
      buffer.data = *file_data;
    }
  }

  return gltf;
}

Result<Gltf, GltfErrorInfo> gltf_parse_file(NotNull<Arena *> arena, Path path) {
  IoResult<Span<u8>> file_data = read<u8>(arena, path);
  if (!file_data) {
    return GltfErrorInfo{.error = GltfError::InvalidFormat,
                         .desc = "Invalid json file."};
  }

  Path base_path = path.parent();
  if (!base_path) {
    base_path = Path::init(".");
  }

  Result<Gltf, GltfErrorInfo> gltf = gltf_parse(arena, *file_data, base_path);
  if (!gltf) {
    return gltf.error();
  }

  return gltf;
}

static JsonValue serialize_asset(NotNull<Arena *> arena,
                                 const GltfAsset &asset) {
  DynamicArray<JsonKeyValue> kv_pairs;
  kv_pairs.push(arena, {"version", JsonValue::init(arena, asset.version)});

  if (asset.generator) {
    kv_pairs.push(arena,
                  {"generator", JsonValue::init(arena, asset.generator)});
  }
  if (asset.copyright) {
    kv_pairs.push(arena,
                  {"copyright", JsonValue::init(arena, asset.copyright)});
  }
  if (asset.min_version) {
    kv_pairs.push(arena,
                  {"minVersion", JsonValue::init(arena, asset.min_version)});
  }

  return JsonValue::init(
      Span<const JsonKeyValue>(kv_pairs.m_data, kv_pairs.m_size));
}

static JsonValue serialize_scene(NotNull<Arena *> arena,
                                 const GltfScene &scene) {
  DynamicArray<JsonKeyValue> kv_pairs;

  if (scene.name) {
    kv_pairs.push(arena, {"name", JsonValue::init(arena, scene.name)});
  }

  if (scene.nodes.m_size > 0) {
    DynamicArray<JsonValue> node_arr;
    for (i32 node : scene.nodes) {
      node_arr.push(arena, JsonValue::init((i64)node));
    }
    kv_pairs.push(arena, {"nodes", JsonValue::init(Span<const JsonValue>(
                                       node_arr.m_data, node_arr.m_size))});
  }

  return JsonValue::init(
      Span<const JsonKeyValue>(kv_pairs.m_data, kv_pairs.m_size));
}

static JsonValue serialize_node(NotNull<Arena *> arena, const GltfNode &node) {
  DynamicArray<JsonKeyValue> kv_pairs;

  if (node.name) {
    kv_pairs.push(arena, {"name", JsonValue::init(arena, node.name)});
  }

  if (node.camera >= 0) {
    kv_pairs.push(arena, {"camera", JsonValue::init((i64)node.camera)});
  }

  if (node.mesh >= 0) {
    kv_pairs.push(arena, {"mesh", JsonValue::init((i64)node.mesh)});
  }

  if (node.skin >= 0) {
    kv_pairs.push(arena, {"skin", JsonValue::init((i64)node.skin)});
  }

  if (node.matrix != glm::identity<glm::mat4>()) {
    DynamicArray<JsonValue> matrix_arr;
    float buffer[16];
    memcpy(buffer, &node.matrix[0], sizeof(glm::mat4));

    for (usize i = 0; i < 16; ++i) {
      JsonValue val;
      val.type = JsonType::Number;
      val.number = buffer[i];
      matrix_arr.push(arena, val);
    }
    kv_pairs.push(arena,
                  {"matrix", JsonValue::init(Span<const JsonValue>(
                                 matrix_arr.m_data, matrix_arr.m_size))});
  } else {
    bool has_translation = node.translation[0] != 0.0f ||
                           node.translation[1] != 0.0f ||
                           node.translation[2] != 0.0f;
    bool has_rotation = node.rotation[0] != 0.0f || node.rotation[1] != 0.0f ||
                        node.rotation[2] != 0.0f || node.rotation[3] != 1.0f;
    bool has_scale =
        node.scale[0] != 1.0f || node.scale[1] != 1.0f || node.scale[2] != 1.0f;

    if (has_translation) {
      DynamicArray<JsonValue> trans_arr;
      for (usize i = 0; i < 3; ++i) {
        JsonValue val;
        val.type = JsonType::Number;
        val.number = node.translation[i];
        trans_arr.push(arena, val);
      }
      kv_pairs.push(arena,
                    {"translation", JsonValue::init(Span<const JsonValue>(
                                        trans_arr.m_data, trans_arr.m_size))});
    }

    if (has_rotation) {
      DynamicArray<JsonValue> rot_arr;
      for (usize i = 0; i < 4; ++i) {
        JsonValue val;
        val.type = JsonType::Number;
        val.number = node.rotation[i];
        rot_arr.push(arena, val);
      }
      kv_pairs.push(arena, {"rotation", JsonValue::init(Span<const JsonValue>(
                                            rot_arr.m_data, rot_arr.m_size))});
    }

    if (has_scale) {
      DynamicArray<JsonValue> scale_arr;
      for (usize i = 0; i < 3; ++i) {
        JsonValue val;
        val.type = JsonType::Number;
        val.number = node.scale[i];
        scale_arr.push(arena, val);
      }
      kv_pairs.push(arena, {"scale", JsonValue::init(Span<const JsonValue>(
                                         scale_arr.m_data, scale_arr.m_size))});
    }
  }

  if (node.children.m_size > 0) {
    DynamicArray<JsonValue> children_arr;
    for (i32 child : node.children) {
      children_arr.push(arena, JsonValue::init((i64)child));
    }
    kv_pairs.push(arena,
                  {"children", JsonValue::init(Span<const JsonValue>(
                                   children_arr.m_data, children_arr.m_size))});
  }

  return JsonValue::init(
      Span<const JsonKeyValue>(kv_pairs.m_data, kv_pairs.m_size));
}

static JsonValue serialize_primitive(NotNull<Arena *> arena,
                                     const GltfPrimitive &prim) {
  DynamicArray<JsonKeyValue> kv_pairs;

  if (prim.attributes.m_size > 0) {
    DynamicArray<JsonKeyValue> attrs;
    for (const GltfAttribute &attr : prim.attributes) {
      attrs.push(arena, {attr.name, JsonValue::init((i64)attr.accessor)});
    }
    kv_pairs.push(arena,
                  {"attributes", JsonValue::init(Span<const JsonKeyValue>(
                                     attrs.m_data, attrs.m_size))});
  }

  if (prim.indices >= 0) {
    kv_pairs.push(arena, {"indices", JsonValue::init((i64)prim.indices)});
  }

  if (prim.material >= 0) {
    kv_pairs.push(arena, {"material", JsonValue::init((i64)prim.material)});
  }

  if (prim.mode != GLTF_TOPOLOGY_TRIANGLES) {
    kv_pairs.push(arena, {"mode", JsonValue::init((i64)prim.mode)});
  }

  return JsonValue::init(
      Span<const JsonKeyValue>(kv_pairs.m_data, kv_pairs.m_size));
}

static JsonValue serialize_mesh(NotNull<Arena *> arena, const GltfMesh &mesh) {
  DynamicArray<JsonKeyValue> kv_pairs;

  if (mesh.name) {
    kv_pairs.push(arena, {"name", JsonValue::init(arena, mesh.name)});
  }

  if (mesh.primitives.m_size > 0) {
    DynamicArray<JsonValue> prim_arr;
    for (const GltfPrimitive &prim : mesh.primitives) {
      prim_arr.push(arena, serialize_primitive(arena, prim));
    }
    kv_pairs.push(arena,
                  {"primitives", JsonValue::init(Span<const JsonValue>(
                                     prim_arr.m_data, prim_arr.m_size))});
  }

  return JsonValue::init(
      Span<const JsonKeyValue>(kv_pairs.m_data, kv_pairs.m_size));
}

static JsonValue serialize_image(NotNull<Arena *> arena,
                                 const GltfImage &image) {
  DynamicArray<JsonKeyValue> kv_pairs;

  if (image.name) {
    kv_pairs.push(arena, {"name", JsonValue::init(arena, image.name)});
  }

  if (image.buffer_view >= 0) {
    kv_pairs.push(arena,
                  {"bufferView", JsonValue::init((i64)image.buffer_view)});
  }

  if (image.mime_type) {
    kv_pairs.push(arena, {"mimeType", JsonValue::init(arena, image.mime_type)});
  }

  if (image.uri) {
    kv_pairs.push(arena, {"uri", JsonValue::init(arena, image.uri)});
  }

  return JsonValue::init(
      Span<const JsonKeyValue>(kv_pairs.m_data, kv_pairs.m_size));
}

static JsonValue serialize_accessor(NotNull<Arena *> arena,
                                    const GltfAccessor &accessor) {
  DynamicArray<JsonKeyValue> kv_pairs;

  if (accessor.name) {
    kv_pairs.push(arena, {"name", JsonValue::init(arena, accessor.name)});
  }

  if (accessor.buffer_view >= 0) {
    kv_pairs.push(arena,
                  {"bufferView", JsonValue::init((i64)accessor.buffer_view)});
  }

  if (accessor.buffer_offset > 0) {
    kv_pairs.push(arena,
                  {"byteOffset", JsonValue::init((i64)accessor.buffer_offset)});
  }

  kv_pairs.push(
      arena, {"componentType", JsonValue::init((i64)accessor.component_type)});

  if (accessor.normalized) {
    JsonValue bool_val;
    bool_val.type = JsonType::Boolean;
    bool_val.boolean = true;
    kv_pairs.push(arena, {"normalized", bool_val});
  }

  kv_pairs.push(arena, {"count", JsonValue::init((i64)accessor.count)});

  String8 type_str;
  i32 element_count = -1;
  switch (accessor.type) {
  case GLTF_TYPE_SCALAR:
    type_str = "SCALAR";
    element_count = 1;
    break;
  case GLTF_TYPE_VEC2:
    type_str = "VEC2";
    element_count = 2;
    break;
  case GLTF_TYPE_VEC3:
    type_str = "VEC3";
    element_count = 3;
    break;
  case GLTF_TYPE_VEC4:
    type_str = "VEC4";
    element_count = 4;
    break;
  case GLTF_TYPE_MAT2:
    type_str = "MAT2";
    element_count = 4;
    break;
  case GLTF_TYPE_MAT3:
    type_str = "MAT3";
    element_count = 9;
    break;
  case GLTF_TYPE_MAT4:
    type_str = "MAT4";
    element_count = 16;
    break;
  }
  kv_pairs.push(arena, {"type", JsonValue::init(arena, type_str)});

  DynamicArray<JsonValue> min_arr;
  for (i32 i = 0; i < element_count; ++i) {
    JsonValue json_val;
    json_val.type = JsonType::Number;
    json_val.number = accessor.min[i];
    min_arr.push(arena, json_val);
  }
  kv_pairs.push(arena, {"min", JsonValue::init(Span<const JsonValue>(
                                   min_arr.m_data, min_arr.m_size))});

  DynamicArray<JsonValue> max_arr;
  for (i32 i = 0; i < element_count; ++i) {
    JsonValue json_val;
    json_val.type = JsonType::Number;
    json_val.number = accessor.max[i];
    max_arr.push(arena, json_val);
  }
  kv_pairs.push(arena, {"max", JsonValue::init(Span<const JsonValue>(
                                   max_arr.m_data, max_arr.m_size))});

  return JsonValue::init(
      Span<const JsonKeyValue>(kv_pairs.m_data, kv_pairs.m_size));
}
static JsonValue serialize_buffer_view(NotNull<Arena *> arena,
                                       const GltfBufferView &view) {
  DynamicArray<JsonKeyValue> kv_pairs;

  if (view.name) {
    kv_pairs.push(arena, {"name", JsonValue::init(arena, view.name)});
  }

  kv_pairs.push(arena, {"buffer", JsonValue::init((i64)view.buffer)});

  if (view.byte_offset > 0) {
    kv_pairs.push(arena,
                  {"byteOffset", JsonValue::init((i64)view.byte_offset)});
  }

  kv_pairs.push(arena, {"byteLength", JsonValue::init((i64)view.byte_length)});

  if (view.byte_stride > 0) {
    kv_pairs.push(arena,
                  {"byteStride", JsonValue::init((i64)view.byte_stride)});
  }

  if (view.target != GLTF_TARGET_NONE) {
    kv_pairs.push(arena, {"target", JsonValue::init((i64)view.target)});
  }

  return JsonValue::init(
      Span<const JsonKeyValue>(kv_pairs.m_data, kv_pairs.m_size));
}

static JsonValue serialize_buffers(NotNull<Arena *> arena,
                                   const GltfBuffer &buffer) {
  DynamicArray<JsonKeyValue> kv_pairs;

  if (buffer.name) {
    kv_pairs.push(arena, {"name", JsonValue::init(arena, buffer.name)});
  }

  if (buffer.uri) {
    kv_pairs.push(arena, {"uri", JsonValue::init(arena, buffer.uri)});
  }

  kv_pairs.push(arena,
                {"byteLength", JsonValue::init((i64)buffer.data.m_size)});

  return JsonValue::init(
      Span<const JsonKeyValue>(kv_pairs.m_data, kv_pairs.m_size));
}

template <typename T>
static JsonValue
serialize_array(NotNull<Arena *> arena, const DynamicArray<T> &arr,
                JsonValue (*serialize_func)(NotNull<Arena *>, const T &)) {
  if (arr.m_size == 0) {
    return {};
  }

  DynamicArray<JsonValue> json_arr;
  for (const T &item : arr) {
    json_arr.push(arena, serialize_func(arena, item));
  }

  return JsonValue::init(
      Span<const JsonValue>(json_arr.m_data, json_arr.m_size));
}

JsonValue gltf_serialize(NotNull<Arena *> arena, const Gltf &gltf) {
  DynamicArray<JsonKeyValue> root;

  root.push(arena, {"asset", serialize_asset(arena, gltf.asset)});

  if (gltf.scene >= 0) {
    root.push(arena, {"scene", JsonValue::init((i64)gltf.scene)});
  }

  if (gltf.scenes.m_size > 0) {
    root.push(arena,
              {"scenes", serialize_array(arena, gltf.scenes, serialize_scene)});
  }

  if (gltf.nodes.m_size > 0) {
    root.push(arena,
              {"nodes", serialize_array(arena, gltf.nodes, serialize_node)});
  }

  if (gltf.meshes.m_size > 0) {
    root.push(arena,
              {"meshes", serialize_array(arena, gltf.meshes, serialize_mesh)});
  }

  if (gltf.images.m_size > 0) {
    root.push(arena,
              {"images", serialize_array(arena, gltf.images, serialize_image)});
  }

  if (gltf.accessors.m_size > 0) {
    root.push(arena, {"accessors", serialize_array(arena, gltf.accessors,
                                                   serialize_accessor)});
  }

  if (gltf.buffer_views.m_size > 0) {
    root.push(arena, {"bufferViews", serialize_array(arena, gltf.buffer_views,
                                                     serialize_buffer_view)});
  }

  if (gltf.buffers.m_size > 0) {
    root.push(arena, {"buffers",
                      serialize_array(arena, gltf.buffers, serialize_buffers)});
  }

  return JsonValue::init(Span<const JsonKeyValue>(root.m_data, root.m_size));
}

String8 gltf_serialize_to_string(NotNull<Arena *> arena, const Gltf &gltf) {
  JsonValue json = gltf_serialize(arena, gltf);
  return json_serialize(arena, json);
}
} // namespace ren