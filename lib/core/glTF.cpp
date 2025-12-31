#include "ren/core/glTF.hpp"
#include "ren/core/JSON.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/Optional.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace ren {
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

static Result<GltfAsset, GltfErrorInfo> gltf_parse_asset(NotNull<Arena *> arena,
                                                         JsonValue json) {
  if (json.type != JsonType::Object) {
    return GltfErrorInfo{
        .error = GltfError::FormatError,
        .desc = format(arena,
                       "An object type is expected for the \"asset\", "
                       "but type is {}.",
                       json.type)};
  }

  GltfAsset asset;
  asset.version = json_string_value_or(json, "version", "");
  if (!asset.version) {
    return GltfErrorInfo{.error = GltfError::FormatError,
                         .desc = "Expected filed \"version\" not found."};
  }

  asset.generator = json_string_value_or(json, "generator", "unknown");
  asset.copyright = json_string_value_or(json, "copyright", "");
  asset.min_version = json_string_value_or(json, "minVersion", "");

  return asset;
}

static Result<GltfScene, GltfErrorInfo> gltf_parse_scene(NotNull<Arena *> arena,
                                                         JsonValue json) {
  GltfScene scene = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{
        .error = GltfError::FormatError,
        .desc = format(arena,
                       "An object type is expected for the \"scene\", "
                       "but type is {}.",
                       json.type)};
  }

  scene.name = json_string_value_or(json, "name", "");

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

static Result<GltfNode, GltfErrorInfo> gltf_parse_node(NotNull<Arena *> arena,
                                                       JsonValue json) {
  GltfNode node = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{
        .error = GltfError::FormatError,
        .desc = format(arena,
                       "An object type is expected for the \"node\", "
                       "but type is {}.",
                       json.type)};
  }

  node.name = json_string_value_or(json, "name", "");
  node.camera = json_integer_value_or(json, "camera", -1);
  node.mesh = json_integer_value_or(json, "mesh", -1);
  node.skin = json_integer_value_or(json, "skin", -1);

  JsonValue matrix_val = json_value(json, "matrix");
  if (matrix_val && matrix_val.type != JsonType::Array) {
    return GltfErrorInfo{.error = GltfError::FormatError,
                         .desc = "\"matrix\" should be array."};
  }

  if (matrix_val) {
    if (Optional<Span<float>> res =
            json_get_float_array(arena, json, "matrix")) {
      node.matrix = glm::make_mat4((*res).begin());
    }
  } else {
    if (Optional<Span<float>> res =
            json_get_float_array(arena, json, "translation")) {
      node.translation = glm::make_vec3((*res).begin());
    }
    if (Optional<Span<float>> res =
            json_get_float_array(arena, json, "rotation")) {
      node.rotation = glm::make_quat((*res).begin());
    }
    if (Optional<Span<float>> res =
            json_get_float_array(arena, json, "scale")) {
      node.scale = glm::make_vec3((*res).begin());
    }
  }

  JsonValue children = json_value(json, "children");
  if (children) {
    if (children.type != JsonType::Array) {
      return GltfErrorInfo{
          .error = GltfError::FormatError,
          .desc = format(arena,
                         "The expected type for the \"children\" field "
                         "is array, but type is {}.",
                         children.type)};
    }

    Span<const JsonValue> child_arr = json_array(children);
    node.children = DynamicArray<i32>::init(arena, child_arr.m_size);

    for (const JsonValue &child : child_arr) {
      if (child.type != JsonType::Integer) {
        return GltfErrorInfo{
            .error = GltfError::FormatError,
            .desc = format(arena,
                           "The expected data type is Integer, but type is {}.",
                           child.type)};
      }
      node.children.push(arena, (i32)child.integer);
    }
  }

  return node;
}

static Result<GltfPrimitive, GltfErrorInfo>
gltf_parse_primitive(NotNull<Arena *> arena, JsonValue json) {
  GltfPrimitive prim = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{
        .error = GltfError::FormatError,
        .desc =
            format(arena, "A \"primitive\" must be an object, but type is {}.",
                   json.type)};
  }

  prim.indices = json_integer_value_or(json, "indices", -1);
  prim.material = json_integer_value_or(json, "material", -1);
  prim.mode = (GltfTopology)json_integer_value_or(json, "mode",
                                                  GLTF_TOPOLOGY_TRIANGLES);

  JsonValue attrs = json_value(json, "attributes");
  if (!attrs) {
    return GltfErrorInfo{
        .error = GltfError::ValidationError,
        .desc = "The \"attributes\" field must be set for \"primitive\"."};
  }

  if (attrs.type != JsonType::Object) {
    return GltfErrorInfo{
        .error = GltfError::FormatError,
        .desc = format(arena,
                       "The expected type for \"attributes\" is an "
                       "object, but type is {}.",
                       attrs.type)};
  }

  prim.attributes = DynamicArray<GltfAttribute>::init(arena, 0);
  Span<const JsonKeyValue> obj = json_object(attrs);
  for (const JsonKeyValue &kv : obj) {
    if (kv.value.type != JsonType::Integer) {
      return GltfErrorInfo{
          .error = GltfError::FormatError,
          .desc = format(
              arena, "The expected accesor type is Integer, but type is {}.",
              kv.value.type)};
    }

    GltfAttribute attr;
    attr.name = kv.key;
    attr.accessor = (i32)kv.value.integer;
    prim.attributes.push(arena, attr);
  }

  return prim;
}

static Result<GltfMesh, GltfErrorInfo> gltf_parse_mesh(NotNull<Arena *> arena,
                                                       JsonValue json) {
  GltfMesh mesh = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{
        .error = GltfError::FormatError,
        .desc = format(arena,
                       "The expected type of the \"mesh\" is an object, "
                       "but type is {}.",
                       json.type)};
  }

  mesh.name = json_string_value_or(json, "name", "");

  JsonValue prims = json_value(json, "primitives");
  if (!prims) {
    return GltfErrorInfo{
        .error = GltfError::ValidationError,
        .desc = "The \"primitives\" field must be set to \"mesh\"."};
  }

  if (prims.type != JsonType::Array) {
    return GltfErrorInfo{
        .error = GltfError::FormatError,
        .desc = format(
            arena,
            "The primitives field is expected to be an array, but type is {}.",
            prims.type)};
  }

  Span<const JsonValue> prim_arr = json_array(prims);
  mesh.primitives = DynamicArray<GltfPrimitive>::init(arena, prim_arr.m_size);

  for (const JsonValue &prim_json : prim_arr) {
    Result<GltfPrimitive, GltfErrorInfo> parse_result =
        gltf_parse_primitive(arena, prim_json);
    if (!parse_result) {
      return parse_result.error();
    }
    mesh.primitives.push(arena, *parse_result);
  }

  return mesh;
}

static Result<GltfImage, GltfErrorInfo> gltf_parse_image(NotNull<Arena *> arena,
                                                         JsonValue json) {
  GltfImage image = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{
        .error = GltfError::FormatError,
        .desc = format(
            arena,
            "An object type is expected for the \"image\", but type is {}.",
            json.type)};
  }

  image.name = json_string_value_or(json, "name", "");
  image.buffer_view = json_integer_value_or(json, "bufferView", -1);
  image.mime_type = json_string_value_or(json, "mimeType", "");
  image.uri = json_string_value_or(json, "uri", "");

  if (image.buffer_view == -1 && !image.uri) {
    return GltfErrorInfo{
        .error = GltfError::FormatError,
        .desc = "One of the fields \"uri\" or \"bufferView\" must be set for \"image\"."};
  }

  return image;
}

static Result<GltfAccessor, GltfErrorInfo>
gltf_parse_accessor(NotNull<Arena *> arena, JsonValue json) {
  GltfAccessor accessor = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::FormatError,
                         .desc = format(arena,
                                        "An object type is expected for the "
                                        "\"accessor\", but type is {}.",
                                        json.type)};
  }

  accessor.name = json_string_value_or(json, "name", "");
  accessor.buffer_view = json_integer_value_or(json, "bufferView", -1);
  if (accessor.buffer_view == -1) {
    return GltfErrorInfo{
        .error = GltfError::ValidationError,
        .desc = "The value \"bufferView\" cannot be missing for \"accessor\""};
  }
  accessor.buffer_offset = json_integer_value_or(json, "byteOffset", 0);
  accessor.component_type =
      (GltfComponentType)json_integer_value_or(json, "componentType", 0);
  if (accessor.component_type == 0) {
    return GltfErrorInfo{
        .error = GltfError::ValidationError,
        .desc = "The value \"componentType\" cannot be missing for \"accessor\""};
  }
  accessor.normalized = json_bool_value_or(json, "normalized", false);
  accessor.count = json_integer_value_or(json, "count", 0);

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
  } else {
    return GltfErrorInfo{.error = GltfError::ValidationError,
                         .desc =
                             format(arena, "Unknown data type: {}.", type_str)};
  }

  JsonValue min_arr = json_value(json, "min");
  if (min_arr.type == JsonType::Array) {
    Span<const JsonValue> min_values = json_array(min_arr);
    if (min_values.m_size > 16) {
      return GltfErrorInfo{.error = GltfError::ValidationError,
                           .desc = "The maximum array size is 16."};
    }
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
    if (max_values.m_size > 16) {
      return GltfErrorInfo{.error = GltfError::ValidationError,
                           .desc = "The maximum array size is 16."};
    }
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

static Result<GltfBufferView, GltfErrorInfo>
gltf_parse_buffer_view(NotNull<Arena *> arena,
                                        JsonValue json) {
  GltfBufferView view = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::FormatError,
                         .desc = format(arena,
                                        "An object type is expected for the "
                                        "\"buffer_view\", but type is {}.",
                                        json.type)};
  }

  view.name = json_string_value_or(json, "name", "");
  view.buffer = json_integer_value_or(json, "buffer", -1);
  if (view.buffer == -1) {
    return GltfErrorInfo{
        .error = GltfError::ValidationError,
        .desc = "The \"buffer\" field is required for \"buffer_view\"."};
  }
  view.byte_offset = json_integer_value_or(json, "byteOffset", 0);
  view.byte_length = json_integer_value_or(json, "byteLength", 0);
  if (view.byte_length == 0) {
    return GltfErrorInfo{
        .error = GltfError::ValidationError,
        .desc = "The \"byteLength\" field is required for \"bufferView\"."};
  }
  view.byte_stride = json_integer_value_or(json, "byteStride", 0);
  view.target = (GltfBufferTarget)json_integer_value_or(json, "target", 0);

  return view;
}

static Result<GltfBuffer, GltfErrorInfo>
gltf_parse_buffer(NotNull<Arena *> arena, JsonValue json) {
  GltfBuffer buffer = {};

  if (json.type != JsonType::Object) {
    return GltfErrorInfo{.error = GltfError::FormatError,
                         .desc = format(arena,
                                        "An object type is expected for the "
                                        "\"buffer\", but type is {}.",
                                        json.type)};
  }

  buffer.name = json_string_value_or(json, "name", "");
  buffer.uri = json_string_value_or(json, "uri", "");

  i64 byte_len = json_integer_value_or(json, "byteLength", -1);
  if (byte_len == -1) {
    return GltfErrorInfo{
        .error = GltfError::ValidationError,
        .desc = "The \"byteLength\" field is required for \"buffer\"."};
  }

  return buffer;
}

template <typename T>
static Result<DynamicArray<T>, GltfErrorInfo> gltf_parse_array(
    NotNull<Arena *> arena, JsonValue arr) {
  if (arr.type != JsonType::Array) {
    return GltfErrorInfo{.error = GltfError::FormatError,
                         .desc =
                             format(arena,
                                    "The expected value type is an array, but "
                                    "the received object type is: {}.",
                                    arr.type)};
  }

  Span<const JsonValue> values = json_array(arr);
  DynamicArray<T> out = DynamicArray<T>::init(arena, values.m_size);

  for (const JsonValue &val : values) {
    Result<T, GltfErrorInfo> parse_result = [&]() -> Result<T, GltfErrorInfo> {
      if constexpr (std::is_same_v<T, GltfScene>) {
        return gltf_parse_scene(arena, val);
      } else if constexpr (std::is_same_v<T, GltfNode>) {
        return gltf_parse_node(arena, val);
      } else if constexpr (std::is_same_v<T, GltfMesh>) {
        return gltf_parse_mesh(arena, val);
      } else if constexpr (std::is_same_v<T, GltfImage>) {
        return gltf_parse_image(arena, val);
      } else if constexpr (std::is_same_v<T, GltfAccessor>) {
        return gltf_parse_accessor(arena, val);
      } else if constexpr (std::is_same_v<T, GltfBufferView>) {
        return gltf_parse_buffer_view(arena, val);
      } else if constexpr (std::is_same_v<T, GltfBuffer>) {
        return gltf_parse_buffer(arena, val);
      } else {
        return GltfErrorInfo{.error = GltfError::InvalidFormat};
      }
    }();

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
    JsonErrorInfo error_info = json.error();
    return GltfErrorInfo{
        .error = GltfError::JSON,
        .desc = format(arena, "Failed to parse gltf file. {} at {}:{}.",
                       error_info.error, error_info.line, error_info.column)};
  }

  if (json->type != JsonType::Object) {
    return GltfErrorInfo{
        .error = GltfError::FormatError,
        .desc = format(arena, "Object type expected, but received: {}",
                       json->type)};
  }

  Gltf gltf{};

  Result<GltfAsset, GltfErrorInfo> asset_parse_result =
      gltf_parse_asset(arena, json_value(*json, "asset"));
  if (!asset_parse_result) {
    return asset_parse_result.error();
  }
  gltf.asset = *asset_parse_result;

  gltf.scene = json_integer_value_or(*json, "scene", -1);

  JsonValue arr;
  arr = json_value(*json, "scenes");
  if (arr) {
    Result<DynamicArray<GltfScene>, GltfErrorInfo> parse_result =
        gltf_parse_array<GltfScene>(arena, arr);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.scenes = *parse_result;
  }
  arr = json_value(*json, "nodes");
  if (arr) {
    Result<DynamicArray<GltfNode>, GltfErrorInfo> parse_result =
        gltf_parse_array<GltfNode>(arena, arr);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.nodes = *parse_result;
  }
  arr = json_value(*json, "meshes");
  if (arr) {
    Result<DynamicArray<GltfMesh>, GltfErrorInfo> parse_result =
        gltf_parse_array<GltfMesh>(arena, arr);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.meshes = *parse_result;
  }
  arr = json_value(*json, "images");
  if (arr) {
    Result<DynamicArray<GltfImage>, GltfErrorInfo> parse_result =
        gltf_parse_array<GltfImage>(arena, arr);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.images = *parse_result;
  }
  arr = json_value(*json, "accessors");
  if (arr) {
    Result<DynamicArray<GltfAccessor>, GltfErrorInfo> parse_result =
        gltf_parse_array<GltfAccessor>(arena, arr);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.accessors = *parse_result;
  }
  arr = json_value(*json, "bufferViews");
  if (arr) {
    Result<DynamicArray<GltfBufferView>, GltfErrorInfo> parse_result =
        gltf_parse_array<GltfBufferView>(arena, arr);
    if (!parse_result) {
      return parse_result.error();
    }
    gltf.buffer_views = *parse_result;
  }
  arr = json_value(*json, "buffers");
  if (arr) {
    Result<DynamicArray<GltfBuffer>, GltfErrorInfo> parse_result =
        gltf_parse_array<GltfBuffer>(arena, arr);
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
                             .desc = "The file could not be opened."};
      }
      buffer.data = *file_data;
    }
  }

  return gltf;
}

Result<Gltf, GltfErrorInfo> load_gltf(NotNull<Arena *> arena, Path path) {
  IoResult<Span<u8>> file_data = read<u8>(arena, path);
  if (!file_data) {
    return GltfErrorInfo{.error = GltfError::IO,
                         .desc = "The file could not be opened."};
  }

  Path base_path = path.parent();

  Result<Gltf, GltfErrorInfo> gltf = gltf_parse(arena, *file_data, base_path);
  if (!gltf) {
    return gltf.error();
  }

  return gltf;
}

static JsonValue gltf_serialize_asset(NotNull<Arena *> arena,
                                 const GltfAsset &asset) {
  DynamicArray<JsonKeyValue> kv_pairs = {};
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

  return JsonValue::init(kv_pairs);
}

static JsonValue gltf_serialize_scene(NotNull<Arena *> arena,
                                 const GltfScene &scene) {
  DynamicArray<JsonKeyValue> kv_pairs = {};

  if (scene.name) {
    kv_pairs.push(arena, {"name", JsonValue::init(arena, scene.name)});
  }

  if (scene.nodes.m_size > 0) {
    DynamicArray<JsonValue> node_arr = {};
    for (i32 node : scene.nodes) {
      node_arr.push(arena, JsonValue::init((i64)node));
    }
    kv_pairs.push(arena, {"nodes", JsonValue::init(Span<const JsonValue>(
                                       node_arr.m_data, node_arr.m_size))});
  }

  return JsonValue::init(kv_pairs);
}

static JsonValue gltf_serialize_node(NotNull<Arena *> arena,
                                     const GltfNode &node) {
  DynamicArray<JsonKeyValue> kv_pairs = {};

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
    DynamicArray<JsonValue> matrix_arr = {};
    matrix_arr.reserve(arena, 16);
    const float *matrix_ptr = glm::value_ptr(node.matrix);

    for (usize i = 0; i < 16; ++i) {
      matrix_arr.push(JsonValue::init(matrix_ptr[i]));
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
      DynamicArray<JsonValue> trans_arr = {};
      for (usize i = 0; i < 3; ++i) {
        trans_arr.push(arena, JsonValue::init(node.translation[i]));
      }
      kv_pairs.push(arena,
                    {"translation", JsonValue::init(Span<const JsonValue>(
                                        trans_arr.m_data, trans_arr.m_size))});
    }

    if (has_rotation) {
      DynamicArray<JsonValue> rot_arr = {};
      for (usize i = 0; i < 4; ++i) {
        rot_arr.push(arena, JsonValue::init(node.rotation[i]));
      }
      kv_pairs.push(arena, {"rotation", JsonValue::init(Span<const JsonValue>(
                                            rot_arr.m_data, rot_arr.m_size))});
    }

    if (has_scale) {
      DynamicArray<JsonValue> scale_arr = {};
      for (usize i = 0; i < 3; ++i) {
        scale_arr.push(arena, JsonValue::init(node.scale[i]));
      }
      kv_pairs.push(arena, {"scale", JsonValue::init(Span<const JsonValue>(
                                         scale_arr.m_data, scale_arr.m_size))});
    }
  }

  if (node.children.m_size > 0) {
    DynamicArray<JsonValue> children_arr = {};
    for (i32 child : node.children) {
      children_arr.push(arena, JsonValue::init((i64)child));
    }
    kv_pairs.push(arena,
                  {"children", JsonValue::init(Span<const JsonValue>(
                                   children_arr.m_data, children_arr.m_size))});
  }

  return JsonValue::init(kv_pairs);
}

static JsonValue gltf_serialize_primitive(NotNull<Arena *> arena,
                                     const GltfPrimitive &prim) {
  DynamicArray<JsonKeyValue> kv_pairs = {};

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

  return JsonValue::init(kv_pairs);
}

static JsonValue gltf_serialize_mesh(NotNull<Arena *> arena,
                                     const GltfMesh &mesh) {
  DynamicArray<JsonKeyValue> kv_pairs = {};

  if (mesh.name) {
    kv_pairs.push(arena, {"name", JsonValue::init(arena, mesh.name)});
  }

  if (mesh.primitives.m_size > 0) {
    DynamicArray<JsonValue> prim_arr;
    for (const GltfPrimitive &prim : mesh.primitives) {
      prim_arr.push(arena, gltf_serialize_primitive(arena, prim));
    }
    kv_pairs.push(arena,
                  {"primitives", JsonValue::init(Span<const JsonValue>(
                                     prim_arr.m_data, prim_arr.m_size))});
  }

  return JsonValue::init(kv_pairs);
}

static JsonValue gltf_serialize_image(NotNull<Arena *> arena,
                                 const GltfImage &image) {
  DynamicArray<JsonKeyValue> kv_pairs = {};

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

  return JsonValue::init(kv_pairs);
}

static JsonValue gltf_serialize_accessor(NotNull<Arena *> arena,
                                    const GltfAccessor &accessor) {
  DynamicArray<JsonKeyValue> kv_pairs = {};

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

  kv_pairs.push(arena, {"normalized", JsonValue::init(accessor.normalized)});

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

  DynamicArray<JsonValue> min_arr = {};
  for (i32 i = 0; i < element_count; ++i) {
    min_arr.push(arena, JsonValue::init(accessor.min[i]));
  }
  kv_pairs.push(arena, {"min", JsonValue::init(Span<const JsonValue>(
                                   min_arr.m_data, min_arr.m_size))});

  DynamicArray<JsonValue> max_arr = {};
  for (i32 i = 0; i < element_count; ++i) {
    max_arr.push(arena, JsonValue::init(accessor.max[i]));
  }
  kv_pairs.push(arena, {"max", JsonValue::init(Span<const JsonValue>(
                                   max_arr.m_data, max_arr.m_size))});

  return JsonValue::init(kv_pairs);
}
static JsonValue gltf_serialize_buffer_view(NotNull<Arena *> arena,
                                       const GltfBufferView &view) {
  DynamicArray<JsonKeyValue> kv_pairs = {};

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

  return JsonValue::init(kv_pairs);
}

static JsonValue gltf_serialize_buffers(NotNull<Arena *> arena,
                                   const GltfBuffer &buffer) {
  DynamicArray<JsonKeyValue> kv_pairs = {};

  if (buffer.name) {
    kv_pairs.push(arena, {"name", JsonValue::init(arena, buffer.name)});
  }

  if (buffer.uri) {
    kv_pairs.push(arena, {"uri", JsonValue::init(arena, buffer.uri)});
  }

  kv_pairs.push(arena,
                {"byteLength", JsonValue::init((i64)buffer.data.m_size)});

  return JsonValue::init(kv_pairs);
}

template <typename T>
static JsonValue
gltf_serialize_array(NotNull<Arena *> arena, const DynamicArray<T> &arr) {
  if (arr.m_size == 0) {
    return {};
  }

  DynamicArray<JsonValue> json_arr = {};
  for (const T &item : arr) {
    if constexpr (std::is_same_v<T, GltfScene>) {
      json_arr.push(arena, gltf_serialize_scene(arena, item));
    } else if constexpr (std::is_same_v<T, GltfNode>) {
      json_arr.push(arena, gltf_serialize_node(arena, item));
    } else if constexpr (std::is_same_v<T, GltfMesh>) {
      json_arr.push(arena, gltf_serialize_mesh(arena, item));
    } else if constexpr (std::is_same_v<T, GltfImage>) {
      json_arr.push(arena, gltf_serialize_image(arena, item));
    } else if constexpr (std::is_same_v<T, GltfAccessor>) {
      json_arr.push(arena, gltf_serialize_accessor(arena, item));
    } else if constexpr (std::is_same_v<T, GltfBufferView>) {
      json_arr.push(arena, gltf_serialize_buffer_view(arena, item));
    } else if constexpr (std::is_same_v<T, GltfBuffer>) {
      json_arr.push(arena, gltf_serialize_buffers(arena, item));
    } else {
      static_assert(false,
                    "Unsupported value type. Should be part of Gltf library.");
    }
  }

  return JsonValue::init(json_arr);
}

JsonValue gltf_serialize(NotNull<Arena *> arena, const Gltf &gltf) {
  DynamicArray<JsonKeyValue> root = {};

  root.push(arena, {"asset", gltf_serialize_asset(arena, gltf.asset)});

  if (gltf.scene >= 0) {
    root.push(arena, {"scene", JsonValue::init((i64)gltf.scene)});
  }

  if (gltf.scenes.m_size > 0) {
    root.push(arena, {"scenes", gltf_serialize_array(arena, gltf.scenes)});
  }

  if (gltf.nodes.m_size > 0) {
    root.push(arena, {"nodes", gltf_serialize_array(arena, gltf.nodes)});
  }

  if (gltf.meshes.m_size > 0) {
    root.push(arena, {"meshes", gltf_serialize_array(arena, gltf.meshes)});
  }

  if (gltf.images.m_size > 0) {
    root.push(arena, {"images", gltf_serialize_array(arena, gltf.images)});
  }

  if (gltf.accessors.m_size > 0) {
    root.push(arena, {"accessors", gltf_serialize_array(arena, gltf.accessors)});
  }

  if (gltf.buffer_views.m_size > 0) {
    root.push(arena,
              {"bufferViews", gltf_serialize_array(arena, gltf.buffer_views)});
  }

  if (gltf.buffers.m_size > 0) {
    root.push(arena, {"buffers", gltf_serialize_array(arena, gltf.buffers)});
  }

  return JsonValue::init(Span<const JsonKeyValue>(root.m_data, root.m_size));
}

String8 to_string(NotNull<Arena *> arena, const Gltf &gltf) {
  JsonValue json = gltf_serialize(arena, gltf);
  return json_serialize(arena, json);
}
} // namespace ren