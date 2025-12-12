#include "ren/core/glTF.hpp"
#include "ren/core/JSON.hpp"

namespace ren {
static inline bool try_get_json_string(JsonValue json, String8 key,
                                       String8 &out) {
  JsonValue val = json_value(json, key);
  if (val.type == JsonType::String) {
    out = json_string(val);
    return true;
  }
  return false;
}
static inline i32 json_get_int(JsonValue json, String8 key, i32 default_val) {
  JsonValue val = json_value(json, key);
  if (val.type == JsonType::Integer) {
    return (i32)json_integer(val);
  }
  return default_val;
}
static inline float json_get_float(JsonValue json, String8 key,
                                   float default_val) {
  JsonValue val = json_value(json, key);
  if (val.type == JsonType::Number) {
    return (float)val.number;
  } else if (val.type == JsonType::Integer) {
    return (float)val.integer;
  }
  return default_val;
}
static inline bool try_get_json_string_required(JsonValue json, String8 key,
                                                String8 &out) {
  JsonValue val = json_value(json, key);
  if (val.type != JsonType::Null) {
    out = json_string(val);
    return true;
  }
  return false;
}
static inline bool json_get_bool(JsonValue json, String8 key,
                                 bool default_val) {
  JsonValue val = json_value(json, key);
  if (val.type == JsonType::Boolean) {
    return val.boolean;
  }
  return default_val;
}
static bool json_get_float_array(JsonValue json, String8 key, float *out,
                                 i32 count) {
  JsonValue val = json_value(json, key);
  if (val.type != JsonType::Array) {
    return false;
  }

  Span<const JsonValue> arr = json_array(val);
  if ((i32)arr.m_size != count) {
    return false;
  }

  for (i32 i = 0; i < count; i++) {
    if (arr[i].type == JsonType::Number) {
      out[i] = (float)arr[i].number;
    } else if (arr[i].type == JsonType::Integer) {
      out[i] = (float)arr[i].integer;
    } else {
      return false;
    }
  }
  return true;
}

static bool parse_asset(JsonValue json, GltfAsset &asset) {
  if (json.type != JsonType::Object) {
    return false;
  }

  if (!try_get_json_string(json, "version", asset.version)) {
    return false;
  }

  try_get_json_string(json, "generator", asset.generator);
  try_get_json_string(json, "copyright", asset.copyright);
  try_get_json_string(json, "minVersion", asset.min_version);

  return true;
}
static GltfScene parse_scene(NotNull<Arena *> arena, JsonValue json) {
  GltfScene scene = {};

  if (json.type != JsonType::Object) {
    return scene;
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
static GltfNode parse_node(NotNull<Arena *> arena, JsonValue json) {
  GltfNode node = {};

  if (json.type != JsonType::Object) {
    return node;
  }

  try_get_json_string(json, "name", node.name);
  node.camera = json_get_int(json, "camera", -1);
  node.mesh = json_get_int(json, "mesh", -1);
  node.skin = json_get_int(json, "skin", -1);

  JsonValue matrix_val = json_value(json, "matrix");
  node.has_matrix = (matrix_val.type == JsonType::Array);

  if (node.has_matrix) {
    json_get_float_array(json, "matrix", node.matrix, 16);
  } else {
    node.translation[0] = 0.0f;
    node.translation[1] = 0.0f;
    node.translation[2] = 0.0f;
    node.rotation[0] = 0.0f;
    node.rotation[1] = 0.0f;
    node.rotation[2] = 0.0f;
    node.rotation[3] = 1.0f;
    node.scale[0] = 1.0f;
    node.scale[1] = 1.0f;
    node.scale[2] = 1.0f;

    json_get_float_array(json, "translation", node.translation, 3);
    json_get_float_array(json, "rotation", node.rotation, 4);
    json_get_float_array(json, "scale", node.scale, 3);
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
static GltfPrimitive parse_primitive(NotNull<Arena *> arena, JsonValue json) {
  GltfPrimitive prim = {};

  if (json.type != JsonType::Object) {
    return prim;
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
static GltfMesh parse_mesh(NotNull<Arena *> arena, JsonValue json) {
  GltfMesh mesh = {};

  if (json.type != JsonType::Object) {
    return mesh;
  }

  try_get_json_string(json, "name", mesh.name);

  JsonValue prims = json_value(json, "primitives");
  if (prims.type == JsonType::Array) {
    Span<const JsonValue> prim_arr = json_array(prims);
    mesh.primitives = DynamicArray<GltfPrimitive>::init(arena, prim_arr.m_size);

    for (const JsonValue &prim_json : prim_arr) {
      GltfPrimitive prim = parse_primitive(arena, prim_json);
      mesh.primitives.push(arena, prim);
    }
  }

  JsonValue weights = json_value(json, "weights");
  if (weights.type == JsonType::Array) {
    Span<const JsonValue> weight_arr = json_array(weights);
    mesh.weights = DynamicArray<float>::init(arena, weight_arr.m_size);

    for (const JsonValue &weight_json : weight_arr) {
      float weight = 0.0f;
      if (weight_json.type == JsonType::Number) {
        weight = (float)weight_json.number;
      } else if (weight_json.type == JsonType::Integer) {
        weight = (float)weight_json.integer;
      }
      mesh.weights.push(arena, weight);
    }
  }

  return mesh;
}
static GltfAccessor parse_accessor(NotNull<Arena *> arena, JsonValue json) {
  GltfAccessor accessor = {};

  if (json.type != JsonType::Object) {
    return accessor;
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
    accessor.min = DynamicArray<float>::init(arena, min_values.m_size);
    for (i32 i = 0; i < min_values.m_size; i++) {
      if (min_values[i].type == JsonType::Number) {
        accessor.min.push(arena, (float)min_values[i].number);
      } else if (min_values[i].type == JsonType::Integer) {
        accessor.min.push(arena, (float)min_values[i].integer);
      }
    }
  }

  JsonValue max_arr = json_value(json, "max");
  if (max_arr.type == JsonType::Array) {
    Span<const JsonValue> max_values = json_array(max_arr);
    accessor.max = DynamicArray<float>::init(arena, max_values.m_size);
    for (i32 i = 0; i < max_values.m_size; i++) {
      if (max_values[i].type == JsonType::Number) {
        accessor.max.push(arena, (float)max_values[i].number);
      } else if (max_values[i].type == JsonType::Integer) {
        accessor.max.push(arena, (float)max_values[i].integer);
      }
    }
  }

  return accessor;
}
static GltfBufferView parse_buffer_view(NotNull<Arena *> arena,
                                        JsonValue json) {
  GltfBufferView view = {};

  if (json.type != JsonType::Object) {
    return view;
  }

  try_get_json_string(json, "name", view.name);
  view.buffer = json_get_int(json, "buffer", 0);
  view.byte_offset = json_get_int(json, "byteOffset", 0);
  view.byte_length = json_get_int(json, "byteLength", 0);
  view.byte_stride = json_get_int(json, "byteStride", 0);
  view.target = (GltfBufferTarget)json_get_int(json, "target", 0);

  return view;
}
static GltfBuffer parse_buffer(NotNull<Arena *> arena, JsonValue json) {
  GltfBuffer buffer = {};

  if (json.type != JsonType::Object) {
    return buffer;
  }

  try_get_json_string(json, "name", buffer.name);
  try_get_json_string(json, "uri", buffer.uri);
  buffer.byte_length = json_get_int(json, "byteLength", 0);

  return buffer;
}

template <typename T>
static void parse_array(NotNull<Arena *> arena, JsonValue arr,
                        DynamicArray<T> &out,
                        T (*parse_func)(NotNull<Arena *>, JsonValue)) {
  if (arr.type != JsonType::Array) {
    return;
  }

  Span<const JsonValue> values = json_array(arr);
  out = DynamicArray<T>::init(arena, values.m_size);

  for (const JsonValue &val : values) {
    T item = parse_func(arena, val);
    out.push(arena, item);
  }
}

static Result<Gltf, GltfError> gltf_parse_json(NotNull<Arena *> arena,
                                               JsonValue json) {
  if (json.type != JsonType::Object) {
    return GLTF_ERROR_INVALID_SOURCE;
  }

  Gltf gltf{};

  JsonValue asset_json = json_value(json, "asset");
  if (!parse_asset(asset_json, gltf.asset)) {
    return GLTF_ERROR_INVALID_SOURCE;
  }

  gltf.scene = json_get_int(json, "scene", -1);

  JsonValue arr;

  arr = json_value(json, "scenes");
  if (arr) {
    parse_array(arena, arr, gltf.scenes, parse_scene);
  }
  arr = json_value(json, "nodes");
  if (arr) {
    parse_array(arena, arr, gltf.nodes, parse_node);
  }
  arr = json_value(json, "meshes");
  if (arr) {
    parse_array(arena, arr, gltf.meshes, parse_mesh);
  }
  arr = json_value(json, "accessors");
  if (arr) {
    parse_array(arena, arr, gltf.accessors, parse_accessor);
  }
  arr = json_value(json, "bufferViews");
  if (arr) {
    parse_array(arena, arr, gltf.buffer_views, parse_buffer_view);
  }
  arr = json_value(json, "buffers");
  if (arr) {
    parse_array(arena, arr, gltf.buffers, parse_buffer);
  }

  return gltf;
}
static bool is_data_uri(String8 uri) { return uri.starts_with("data:"); }
static Result<Span<u8>, GltfError> decode_data_uri(NotNull<Arena *> arena,
                                                   String8 uri) {
  String8 base64_marker = uri.find(",");
  if (!base64_marker) {
    return GLTF_ERROR_INVALID_SOURCE;
  }

  String8 base64_data = uri.remove_prefix(base64_marker.m_str - uri.m_str + 1);

  static const i8 decode_table[128] = {
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
      52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
      -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
      15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
      -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
      41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1};

  usize max_output_size = (base64_data.m_size * 3) / 4 + 3;
  u8 *output = arena->allocate<u8>(max_output_size);
  usize output_idx = 0;

  u32 buffer = 0;
  i32 bits_collected = 0;

  for (usize i = 0; i < base64_data.m_size; ++i) {
    char c = base64_data[i];

    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '=') {
      continue;
    }

    if (c < 0 || c >= 128) {
      return GLTF_ERROR_INVALID_SOURCE;
    }

    i8 value = decode_table[(u8)c];
    if (value == -1) {
      return GLTF_ERROR_INVALID_SOURCE;
    }

    buffer = (buffer << 6) | (u32)value;
    bits_collected += 6;

    if (bits_collected >= 8) {
      bits_collected -= 8;
      output[output_idx++] = (u8)((buffer >> bits_collected) & 0xFF);
    }
  }

  return Span<u8>(output, output_idx);
}
static Result<void, GltfError> gltf_load_buffers(NotNull<Arena *> arena,
                                                 Gltf &gltf, Path base_path) {
  for (GltfBuffer &buffer : gltf.buffers) {
    if (buffer.data.m_size || !buffer.uri) {
      continue;
    }
    if (is_data_uri(buffer.uri)) {
      Result<Span<u8>, GltfError> decoded = decode_data_uri(arena, buffer.uri);
      if (!decoded) {
        return decoded.error();
      }

      buffer.data.m_data = decoded->m_data;
      buffer.data.m_capacity = buffer.data.m_size = decoded->m_size;
      buffer.byte_length = (i32)decoded->m_size;
    } else {
      Path buffer_path = base_path.concat(arena, Path::init(buffer.uri));

      IoResult<Span<u8>> file_data = read<u8>(arena, buffer_path);
      if (!file_data) {
        return GLTF_ERROR_INVALID_SOURCE;
      }

      buffer.data.m_data = file_data->m_data;
      buffer.data.m_capacity = buffer.data.m_size = file_data->m_size;
      buffer.byte_length = (i32)file_data->m_size;
    }
  }

  return {};
}

Result<Gltf, GltfError> gltf_parse(NotNull<Arena *> arena, Span<u8> buffer) {
  Result<JsonValue, JsonErrorInfo> json =
      json_parse(arena, String8((const char *)buffer.m_data, buffer.m_size));
  if (!json) {
    return GLTF_ERROR_INVALID_SOURCE;
  }
  return gltf_parse_json(arena, *json);
}

Result<Gltf, GltfError> gltf_parse_file(NotNull<Arena *> arena, Path path) {
  IoResult<Span<u8>> file_data = read<u8>(arena, path);
  if (!file_data) {
    return GLTF_ERROR_INVALID_SOURCE;
  }

  Result<Gltf, GltfError> gltf = gltf_parse(arena, *file_data);
  if (!gltf) {
    return gltf.error();
  }
  Path base_path = path.parent();
  if (!base_path) {
    base_path = Path::init(".");
  }
  Result<void, GltfError> load_result =
      gltf_load_buffers(arena, *gltf, base_path);
  if (!load_result) {
    return load_result.error();
  }

  return gltf;
}
} // namespace ren