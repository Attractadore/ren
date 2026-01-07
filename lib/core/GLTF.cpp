#include "ren/core/GLTF.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/JSON.hpp"
#include "ren/core/Optional.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <tracy/Tracy.hpp>

namespace ren {

String8 format_as(GltfAttributeSemantic semantic) {
  switch (semantic) {
  case GltfAttributeSemantic::POSITION:
    return "POSITION";
  case GltfAttributeSemantic::NORMAL:
    return "NORMAL";
  case GltfAttributeSemantic::TANGENT:
    return "TANGENT";
  case GltfAttributeSemantic::TEXCOORD:
    return "TEXCOORD";
  case GltfAttributeSemantic::COLOR:
    return "COLOR";
  case GltfAttributeSemantic::JOINTS:
    return "JOINTS";
  case GltfAttributeSemantic::WEIGHTS:
    return "WEIGHTS";
  case GltfAttributeSemantic::USER:
    return "USER";
  }
  unreachable();
}

String8 format_as(GltfTextureFilter filter) {
  switch (filter) {
  case GLTF_TEXTURE_FILTER_NEAREST:
    return "NEAREST";
  case GLTF_TEXTURE_FILTER_LINEAR:
    return "LINEAR";
  case GLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
    return "NEAREST_MIPMAP_NEAREST";
  case GLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
    return "LINEAR_MIPMAP_NEAREST";
  case GLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
    return "NEAREST_MIPMAP_LINEAR";
  case GLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
    return "LINEAR_MIPMAP_LINEAR";
  }
  unreachable();
}

String8 format_as(GltfTextureWrap wrap) {
  switch (wrap) {
  case GLTF_TEXTURE_WRAP_REPEAT:
    return "REPEAT";
  case GLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
    return "CLAMP_TO_EDGE";
  case GLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
    return "MIRRORED_REPEAT";
  }
  unreachable();
}

String8 format_as(GltfTopology topology) {
  switch (topology) {
  case GLTF_TOPOLOGY_POINTS:
    return "POINTS";
  case GLTF_TOPOLOGY_LINES:
    return "LINES";
  case GLTF_TOPOLOGY_LINE_LOOP:
    return "LINE_LOOP";
  case GLTF_TOPOLOGY_LINE_STRIP:
    return "LINE_STRIP";
  case GLTF_TOPOLOGY_TRIANGLES:
    return "TRIANGLES";
  case GLTF_TOPOLOGY_TRIANGLE_STRIP:
    return "TRIANGLE_STRIPS";
  case GLTF_TOPOLOGY_TRIANGLE_FAN:
    return "TRIANGLE_FAN";
  }
  unreachable();
}

Optional<GltfAttribute>
gltf_find_attribute_by_semantic(GltfPrimitive primitive,
                                GltfAttributeSemantic semantic, i32 set_index) {
  for (GltfAttribute attribute : primitive.attributes) {
    if (attribute.semantic == semantic and attribute.set_index == set_index) {
      return attribute;
    }
  }
  return NullOpt;
}

struct GltfParserContext {
  Arena *arena = nullptr;
  Arena *scratch = nullptr;
  DynamicArray<String8> path;
};

class GltfJsonPathScope {
public:
  GltfJsonPathScope(NotNull<GltfParserContext *> ctx, usize i) {
    m_ctx = ctx;
    m_previous = m_ctx->path.back();
    m_ctx->path.back() = format(ctx->scratch, "{}[{}]", m_previous, i);
  }

  GltfJsonPathScope(NotNull<GltfParserContext *> ctx, String8 name) {
    m_ctx = ctx;
    m_ctx->path.push(ctx->scratch, name);
  }

  ~GltfJsonPathScope() {
    if (!m_previous) {
      m_ctx->path.pop();
    } else {
      m_ctx->path.back() = m_previous;
    }
  }

private:
  GltfParserContext *m_ctx = nullptr;
  String8 m_previous;
};

#define GLTF_ERROR(error_value, description, ...)                              \
  GltfErrorInfo {                                                              \
    .error = error_value,                                                      \
    .message = format(ctx->arena, "Failed to parse GLTF: {}: " description,    \
                      String8::join(ctx->scratch, ctx->path, ".")              \
                          __VA_OPT__(, ) __VA_ARGS__),                         \
  }

#define GLTF_ERROR_MISSING_FIELD(field)                                        \
  GLTF_ERROR(GltfError::InvalidFormat, "Missing required field \"{}\"", field)

#define GLTF_JSON_PATH_SCOPE(name)                                             \
  GltfJsonPathScope ren_cat(gltf_json_path_scope_, __COUNTER__)(ctx, name);

#define GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(value, expected)             \
  if (!json_try_cast<expected>(value)) {                                       \
    String8 path = String8::join(ctx->scratch, ctx->path, ".");                \
    path = path ? path : "document";                                           \
    ren_trap();                                                                \
    return GltfErrorInfo{                                                      \
        .error = GltfError::InvalidFormat,                                     \
        .message = format(ctx->arena,                                          \
                          "Failed to parse GLTF: {}: Invalid JSON type: "      \
                          "expected {}, got {}",                               \
                          path, expected, value.type),                         \
    };                                                                         \
  }

#define GLTF_JSON_CAST_OR_RETURN_ERROR(dst, json, expected)                    \
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, expected);                   \
  dst = json_cast<expected>(json);

#define GLTF_WARN_IGNORED                                                      \
  fmt::println("gltf: warn: Ignoring {}",                                      \
               String8::join(ctx->scratch, ctx->path, "."))

#define GLTF_UNIQUE_RESULT ren_cat(result, __LINE__)
#define GLTF_TRY(dst, expr, ...)                                               \
  auto GLTF_UNIQUE_RESULT = expr __VA_OPT__(, ) __VA_ARGS__;                   \
  if (!GLTF_UNIQUE_RESULT) {                                                   \
    return GLTF_UNIQUE_RESULT.error();                                         \
  }                                                                            \
  dst = *GLTF_UNIQUE_RESULT

static Result<GltfVersion, GltfErrorInfo>
gltf_parse_version(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::String);
  GltfVersion version;
  int count = std::sscanf(json_string(json).zero_terminated(ctx->scratch),
                          "%u.%u ", &version.major, &version.minor);
  if (count != 2) {
    return GLTF_ERROR(GltfError::InvalidFormat,
                      "Failed to parse version \"{}\"", json_string(json));
  }
  return version;
}

static Result<GltfAsset, GltfErrorInfo>
gltf_parse_asset(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);

  Optional<GltfVersion> version;
  Optional<GltfVersion> min_version;

  GltfAsset asset;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "version") {
      GLTF_TRY(version, gltf_parse_version(ctx, value));
    } else if (key == "minVersion") {
      GLTF_TRY(min_version, gltf_parse_version(ctx, value));
    } else if (key == "generator") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(asset.generator, value, JsonType::String);
      asset.generator = asset.generator.copy(ctx->arena);
    } else if (key == "copyright") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(asset.copyright, value, JsonType::String);
      asset.copyright = asset.copyright.copy(ctx->arena);
    } else {
      GLTF_WARN_IGNORED;
    }
  }

  if (!version) {
    return GLTF_ERROR_MISSING_FIELD("version");
  }
  if (!min_version) {
    min_version = version;
  }

  if (min_version->major > 2 or
      (min_version->major == 2 and min_version->minor > 0)) {
    return GLTF_ERROR(GltfError::Unsupported, "Unsupported glTF version {}.{}",
                      min_version->major, min_version->minor);
  }

  return asset;
}

static Result<GltfScene, GltfErrorInfo>
gltf_parse_scene(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);

  GltfScene scene;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "name") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(scene.name, value, JsonType::String);
      scene.name = scene.name.copy(ctx->arena);
    } else if (key == "nodes") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(Span<const JsonValue> nodes, value,
                                     JsonType::Array);
      scene.nodes = Span<i32>::allocate(ctx->arena, nodes.size());
      for (usize i : range(nodes.size())) {
        GLTF_JSON_PATH_SCOPE(i);
        GLTF_JSON_CAST_OR_RETURN_ERROR(scene.nodes[i], nodes[i],
                                       JsonType::Integer);
      }
    } else {
      GLTF_WARN_IGNORED;
    }
  }

  return scene;
}

static Result<GltfNode, GltfErrorInfo>
gltf_parse_node(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);

  GltfNode node;
  bool has_matrix = false;
  String8 transform_field;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "name") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(node.name, value, JsonType::String);
      node.name = node.name.copy(ctx->arena);
    } else if (key == "mesh") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(node.mesh, value, JsonType::Integer);
    } else if (key == "matrix") {
      if (transform_field) {
        return GLTF_ERROR(
            GltfError::InvalidFormat,
            "Can't specify matrix when {} was previously specified",
            transform_field);
      }
      GLTF_JSON_CAST_OR_RETURN_ERROR(Span<const JsonValue> matrix, value,
                                     JsonType::Array);
      if (matrix.m_size != 16) {
        return GLTF_ERROR(GltfError::InvalidFormat, "Size must be 16");
      }
      for (usize i : range(16)) {
        GLTF_JSON_PATH_SCOPE(i);
        GLTF_JSON_CAST_OR_RETURN_ERROR(glm::value_ptr(node.matrix)[i],
                                       matrix[i], JsonType::Number);
      }
      has_matrix = true;
    } else if (key == "translation" or key == "rotation" or key == "scale") {
      if (has_matrix) {
        return GLTF_ERROR(
            GltfError::InvalidFormat,
            "Can't specify {} when matrix was previously specified", key);
      }
      GLTF_JSON_CAST_OR_RETURN_ERROR(Span<const JsonValue> array, value,
                                     JsonType::Array);
      if (key == "translation") {
        if (array.size() != 3) {
          return GLTF_ERROR(GltfError::InvalidFormat, "Size must be 3");
        }
        for (usize i : range(3)) {
          GLTF_JSON_PATH_SCOPE(i);
          GLTF_JSON_CAST_OR_RETURN_ERROR(node.translation[i], array[i],
                                         JsonType::Number);
        }
      } else if (key == "rotation") {
        if (array.size() != 4) {
          return GLTF_ERROR(GltfError::InvalidFormat, "Size must be 4");
        }
        float rotation[4];
        for (usize i : range(4)) {
          GLTF_JSON_PATH_SCOPE(i);
          GLTF_JSON_CAST_OR_RETURN_ERROR(rotation[i], array[i],
                                         JsonType::Number);
        }
        node.rotation.x = rotation[0];
        node.rotation.y = rotation[1];
        node.rotation.z = rotation[2];
        node.rotation.w = rotation[3];
        node.rotation = glm::normalize(node.rotation);
      } else {
        ren_assert(key == "scale");
        if (array.size() != 3) {
          return GLTF_ERROR(GltfError::InvalidFormat, "Size must be 3");
        }
        for (usize i : range(3)) {
          GLTF_JSON_PATH_SCOPE(i);
          GLTF_JSON_CAST_OR_RETURN_ERROR(node.scale[i], array[i],
                                         JsonType::Number);
        }
      }
      transform_field = key;
    } else if (key == "children") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(Span<const JsonValue> children, value,
                                     JsonType::Array);
      node.children = Span<i32>::allocate(ctx->arena, children.size());
      for (usize i : range(children.size())) {
        GLTF_JSON_PATH_SCOPE(i);
        GLTF_JSON_CAST_OR_RETURN_ERROR(node.children[i], children[i],
                                       JsonType::Integer);
      }
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  if (not has_matrix) {
    node.matrix = glm::translate(node.matrix, node.translation);
    node.matrix = node.matrix * glm::mat4(node.rotation);
    node.matrix = glm::scale(node.matrix, node.scale);
  }

  return node;
}

static Result<GltfPrimitive, GltfErrorInfo>
gltf_parse_primitive(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);

  GltfPrimitive primitive;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "indices") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(primitive.indices, value,
                                     JsonType::Integer);
    } else if (key == "mode") {
      i32 topology = 0;
      GLTF_JSON_CAST_OR_RETURN_ERROR(topology, value, JsonType::Integer);
      switch (topology) {
      case GLTF_TOPOLOGY_POINTS:
      case GLTF_TOPOLOGY_LINES:
      case GLTF_TOPOLOGY_LINE_LOOP:
      case GLTF_TOPOLOGY_LINE_STRIP:
      case GLTF_TOPOLOGY_TRIANGLES:
      case GLTF_TOPOLOGY_TRIANGLE_STRIP:
      case GLTF_TOPOLOGY_TRIANGLE_FAN:
        break;
      default:
        return GLTF_ERROR(GltfError::InvalidFormat, "Invalid topology value {}",
                          topology);
      }
      primitive.mode = (GltfTopology)topology;
    } else if (key == "attributes") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(Span<const JsonKeyValue> attributes, value,
                                     JsonType::Object);
      if (attributes.is_empty()) {
        return GLTF_ERROR(GltfError::InvalidFormat, "Size must be > 0");
      }
      primitive.attributes =
          Span<GltfAttribute>::allocate(ctx->arena, attributes.size());
      usize i = 0;
      for (auto [attribute_name, attribute_accessor] : attributes) {
        GLTF_JSON_PATH_SCOPE(attribute_name);
        GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(attribute_accessor,
                                                  JsonType::Integer);
        if (attribute_name == "") {
          return GLTF_ERROR(GltfError::InvalidFormat,
                            "Empty attribute name is not allowed");
        }
        Optional<GltfAttributeSemantic> semantic;
        i32 set_index = 0;
        if (attribute_name[0] == '_') {
          semantic = GltfAttributeSemantic::USER;
        } else {
          if (attribute_name == "POSITION") {
            semantic = GltfAttributeSemantic::POSITION;
          } else if (attribute_name == "NORMAL") {
            semantic = GltfAttributeSemantic::NORMAL;
          } else if (attribute_name == "TANGENT") {
            semantic = GltfAttributeSemantic::TANGENT;
          } else {
            const char *underscore_pos = attribute_name.find('_');
            if (!underscore_pos) {
              return GLTF_ERROR(GltfError::InvalidFormat,
                                "Failed to parse attribute semantic");
            }
            usize underscore_offset = underscore_pos - attribute_name.data();
            String8 semantic_str = attribute_name.substr(0, underscore_offset);
            if (semantic_str == "TEXCOORD") {
              semantic = GltfAttributeSemantic::TEXCOORD;
            } else if (semantic_str == "COLOR") {
              semantic = GltfAttributeSemantic::COLOR;
            } else if (semantic_str == "JOINTS") {
              semantic = GltfAttributeSemantic::JOINTS;
            } else if (semantic_str == "WEIGHTS") {
              semantic = GltfAttributeSemantic::WEIGHTS;
            } else {
              return GLTF_ERROR(GltfError::InvalidFormat,
                                "Unknown attribute semantic");
            }
            String8 set_index_str =
                attribute_name.substr(underscore_offset + 1);
            int count = std::sscanf(set_index_str.zero_terminated(ctx->scratch),
                                    "%u", &set_index);
            if (count != 1) {
              return GLTF_ERROR(GltfError::InvalidFormat,
                                "Failed to parse attribute set index");
            }
          }
        }
        primitive.attributes[i++] = {
            .name = attribute_name,
            .semantic = *semantic,
            .set_index = set_index,
            .accessor = (i32)json_integer(attribute_accessor),
        };
      }
    } else if (key == "material") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(primitive.material, value,
                                     JsonType::Integer);
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  if (primitive.attributes.is_empty()) {
    return GLTF_ERROR_MISSING_FIELD("attributes");
  }

  return primitive;
}

static Result<GltfMesh, GltfErrorInfo>
gltf_parse_mesh(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);

  GltfMesh mesh;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "name") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(mesh.name, value, JsonType::String);
      mesh.name = mesh.name.copy(ctx->arena);
    } else if (key == "primitives") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(Span<const JsonValue> primitives, value,
                                     JsonType::Array);
      if (primitives.is_empty()) {
        return GLTF_ERROR(GltfError::InvalidFormat, "Size must be > 0");
      }
      mesh.primitives =
          Span<GltfPrimitive>::allocate(ctx->arena, primitives.size());
      for (usize i : range(primitives.size())) {
        GLTF_JSON_PATH_SCOPE(i);
        GLTF_TRY(mesh.primitives[i], gltf_parse_primitive(ctx, primitives[i]));
      }
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  if (mesh.primitives.is_empty()) {
    return GLTF_ERROR_MISSING_FIELD("primitives");
  }

  return mesh;
}

static Result<GltfImage, GltfErrorInfo>
gltf_parse_image(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);
  GltfImage image;
  bool has_buffer = false;
  bool has_uri = false;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "name") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(image.name, value, JsonType::String);
      image.name = image.name.copy(ctx->arena);
    } else if (key == "bufferView") {
      if (has_uri) {
        return GLTF_ERROR(
            GltfError::InvalidFormat,
            "Can't define \"bufferView\" after \"uri\" has been defined");
      }
      GLTF_JSON_CAST_OR_RETURN_ERROR(image.buffer_view, value,
                                     JsonType::Integer);
      has_buffer = true;
    } else if (key == "mimeType") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(image.mime_type, value, JsonType::String);
      if (image.mime_type != "image/jpeg" and image.mime_type != "image/png") {
        return GLTF_ERROR(GltfError::InvalidFormat, "Invalid mime type {}",
                          image.mime_type);
      }
      image.mime_type = image.mime_type.copy(ctx->arena);
    } else if (key == "uri") {
      if (has_buffer) {
        return GLTF_ERROR(
            GltfError::InvalidFormat,
            "Can't define \"uri\" after \"bufferView\" has been defined");
      }
      GLTF_JSON_CAST_OR_RETURN_ERROR(image.uri, value, JsonType::String);
      if (!image.uri) {
        return GLTF_ERROR(GltfError::InvalidFormat, "Empty uri is not allowed");
      }
      image.uri = image.uri.copy(ctx->arena);
      has_uri = true;
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  if (not has_buffer and not has_uri) {
    return GLTF_ERROR(GltfError::InvalidFormat,
                      "Either \"bufferView\" or \"uri\" must be defined");
  }
  if (image.buffer_view >= 0 and !image.mime_type) {
    return GLTF_ERROR(
        GltfError::InvalidFormat,
        "\"mimeType\" must be defined if \"bufferView\" is defined");
  }
  return image;
}

static Result<GltfTextureInfo, GltfErrorInfo>
gltf_parse_texture_info(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);
  GltfTextureInfo texture_info;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "index") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(texture_info.index, value,
                                     JsonType::Integer);
    } else if (key == "texCoord") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(texture_info.tex_coord, value,
                                     JsonType::Integer);
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  return texture_info;
}

static Result<GltfOcclusionTextureInfo, GltfErrorInfo>
gltf_parse_occlusion_texture_info(NotNull<GltfParserContext *> ctx,
                                  JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);
  GltfOcclusionTextureInfo occlusion_texture_info;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "index") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(occlusion_texture_info.index, value,
                                     JsonType::Integer);
    } else if (key == "texCoord") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(occlusion_texture_info.tex_coord, value,
                                     JsonType::Integer);
    } else if (key == "strength") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(occlusion_texture_info.strength, value,
                                     JsonType::Number);
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  return occlusion_texture_info;
}

static Result<GltfNormalTextureInfo, GltfErrorInfo>
gltf_parse_normal_texture_info(NotNull<GltfParserContext *> ctx,
                               JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);
  GltfNormalTextureInfo normal_texture_info;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "index") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(normal_texture_info.index, value,
                                     JsonType::Integer);
    } else if (key == "texCoord") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(normal_texture_info.tex_coord, value,
                                     JsonType::Integer);
    } else if (key == "scale") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(normal_texture_info.scale, value,
                                     JsonType::Number);
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  return normal_texture_info;
}

static Result<GltfPbrMetallicRoughness, GltfErrorInfo>
gltf_parse_pbr_metallic_roughness(NotNull<GltfParserContext *> ctx,
                                  JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);
  GltfPbrMetallicRoughness pbr_metallic_roughness;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "baseColorFactor") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(Span<const JsonValue> array, value,
                                     JsonType::Array);
      if (array.size() != 4) {
        return GLTF_ERROR(GltfError::InvalidFormat, "Size must be 4");
      }
      for (usize i : range(4)) {
        GLTF_JSON_PATH_SCOPE(i);
        GLTF_JSON_CAST_OR_RETURN_ERROR(
            pbr_metallic_roughness.base_color_factor[i], array[i],
            JsonType::Number);
      }
    } else if (key == "baseColorTexture") {
      GLTF_TRY(pbr_metallic_roughness.base_color_texture,
               gltf_parse_texture_info(ctx, value));
    } else if (key == "roughnessFactor") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(pbr_metallic_roughness.roughness_factor,
                                     value, JsonType::Number);
    } else if (key == "metallicFactor") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(pbr_metallic_roughness.metallic_factor,
                                     value, JsonType::Number);
    } else if (key == "metallicRoughnessTexture") {
      GLTF_TRY(pbr_metallic_roughness.metallic_roughness_texture,
               gltf_parse_texture_info(ctx, value));
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  return pbr_metallic_roughness;
}

static Result<GltfMaterial, GltfErrorInfo>
gltf_parse_material(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);
  GltfMaterial material;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "name") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(material.name, value, JsonType::String);
    } else if (key == "pbrMetallicRoughness") {
      GLTF_TRY(material.pbr_metallic_roughness,
               gltf_parse_pbr_metallic_roughness(ctx, value));
    } else if (key == "emissiveFactor") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(Span<const JsonValue> array, value,
                                     JsonType::Array);
      if (array.size() != 3) {
        return GLTF_ERROR(GltfError::InvalidFormat, "Size must be 3");
      }
      for (usize i : range(3)) {
        GLTF_JSON_PATH_SCOPE(i);
        GLTF_JSON_CAST_OR_RETURN_ERROR(material.emissive_factor[i], array[i],
                                       JsonType::Number);
      }
    } else if (key == "normalTexture") {
      GLTF_TRY(material.normal_texture,
               gltf_parse_normal_texture_info(ctx, value));
    } else if (key == "occlusionTexture") {
      GLTF_TRY(material.occlusion_texture,
               gltf_parse_occlusion_texture_info(ctx, value));
    } else if (key == "emissiveTexture") {
      GLTF_TRY(material.emissive_texture, gltf_parse_texture_info(ctx, value));
    } else if (key == "doubleSided") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(material.doubleSided, value,
                                     JsonType::Boolean);
    } else if (key == "alphaMode") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(String8 alpha_mode, value,
                                     JsonType::String);
      if (alpha_mode == "OPAQUE") {
        material.alphaMode = GLTF_ALPHA_MODE_OPAQUE;
      } else if (alpha_mode == "MASK") {
        material.alphaMode = GLTF_ALPHA_MODE_MASK;
      } else if (alpha_mode == "BLEND") {
        material.alphaMode = GLTF_ALPHA_MODE_BLEND;
      } else {
        return GLTF_ERROR(GltfError::InvalidFormat,
                          "Invalid alpha mode value {}", alpha_mode);
      }
    } else if (key == "alphaCutoff") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(material.alphaCutoff, value,
                                     JsonType::Number);
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  return material;
}

static Result<GltfTexture, GltfErrorInfo>
gltf_parse_texture(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);
  GltfTexture texture;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "sampler") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(texture.sampler, value, JsonType::Integer);
    } else if (key == "source") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(texture.source, value, JsonType::Integer);
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  return texture;
}

static Result<GltfSampler, GltfErrorInfo>
gltf_parse_sampler(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);
  GltfSampler sampler;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "wrapS") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(i64 wrap, value, JsonType::Integer);
      switch (wrap) {
      default:
        return GLTF_ERROR(GltfError::InvalidFormat, "Invalid wrap value {}",
                          wrap);
      case GLTF_TEXTURE_WRAP_REPEAT:
      case GLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
      case GLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
        break;
      }
      sampler.wrap_s = (GltfTextureWrap)wrap;
    } else if (key == "wrapT") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(i64 wrap, value, JsonType::Integer);
      switch (wrap) {
      default:
        return GLTF_ERROR(GltfError::InvalidFormat, "Invalid wrap value {}",
                          wrap);
      case GLTF_TEXTURE_WRAP_REPEAT:
      case GLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
      case GLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
        break;
      }
      sampler.wrap_t = (GltfTextureWrap)wrap;
    } else if (key == "magFilter") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(i64 filter, value, JsonType::Integer);
      switch (filter) {
      default:
        return GLTF_ERROR(GltfError::InvalidFormat,
                          "Invalid magnification filter value {}", filter);
      case GLTF_TEXTURE_FILTER_NEAREST:
      case GLTF_TEXTURE_FILTER_LINEAR:
        break;
      }
      sampler.mag_filter = (GltfTextureFilter)filter;
    } else if (key == "minFilter") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(i64 filter, value, JsonType::Integer);
      switch (filter) {
      default:
        return GLTF_ERROR(GltfError::InvalidFormat,
                          "Invalid minification filter value {}", filter);
      case GLTF_TEXTURE_FILTER_NEAREST:
      case GLTF_TEXTURE_FILTER_LINEAR:
      case GLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
      case GLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
      case GLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
      case GLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
        break;
      }
      sampler.min_filter = (GltfTextureFilter)filter;
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  return sampler;
}

static Result<GltfAccessor, GltfErrorInfo>
gltf_parse_accessor(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);
  GltfAccessor accessor;
  bool has_component_type = false;
  bool has_count = false;
  bool has_type = false;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "name") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(accessor.name, value, JsonType::String);
      accessor.name = accessor.name.copy(ctx->arena);
    } else if (key == "bufferView") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(accessor.buffer_view, value,
                                     JsonType::Integer);
      if (accessor.buffer_view < 0) {
        return GLTF_ERROR(GltfError::InvalidFormat, "Invalid value {}",
                          accessor.buffer_view);
      }
    } else if (key == "byteOffset") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(accessor.byte_offset, value,
                                     JsonType::Integer);
    } else if (key == "componentType") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(i32 component_type, value,
                                     JsonType::Integer);
      switch (component_type) {
      case GLTF_COMPONENT_TYPE_BYTE:
      case GLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
      case GLTF_COMPONENT_TYPE_SHORT:
      case GLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
      case GLTF_COMPONENT_TYPE_UNSIGNED_INT:
      case GLTF_COMPONENT_TYPE_FLOAT:
        break;
      default:
        return GLTF_ERROR(GltfError::InvalidFormat, "Invalid component type {}",
                          component_type);
      }
      accessor.component_type = (GltfComponentType)component_type;
      has_component_type = true;
    } else if (key == "normalized") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(accessor.normalized, value,
                                     JsonType::Boolean);
    } else if (key == "count") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(accessor.count, value, JsonType::Integer);
      if (accessor.count == 0) {
        return GLTF_ERROR(GltfError::InvalidFormat,
                          "Accessor count must be > 0");
      }
      has_count = true;
    } else if (key == "type") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(String8 type, value, JsonType::String);
      if (type == "SCALAR") {
        accessor.type = GLTF_ACCESSOR_TYPE_SCALAR;
      } else if (type == "VEC2") {
        accessor.type = GLTF_ACCESSOR_TYPE_VEC2;
      } else if (type == "VEC3") {
        accessor.type = GLTF_ACCESSOR_TYPE_VEC3;
      } else if (type == "VEC4") {
        accessor.type = GLTF_ACCESSOR_TYPE_VEC4;
      } else if (type == "MAT2") {
        accessor.type = GLTF_ACCESSOR_TYPE_MAT2;
      } else if (type == "MAT3") {
        accessor.type = GLTF_ACCESSOR_TYPE_MAT3;
      } else if (type == "MAT4") {
        accessor.type = GLTF_ACCESSOR_TYPE_MAT4;
      } else {
        return GLTF_ERROR(GltfError::InvalidFormat, "Invalid accessor type {}",
                          type);
      }
      has_type = true;
    } else if (key == "min" or key == "max") {
      // Process later when we know the accessor's type.
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  if (accessor.buffer_view < 0) {
    return GLTF_ERROR_MISSING_FIELD("bufferView");
  }
  if (not has_component_type) {
    return GLTF_ERROR_MISSING_FIELD("componentType");
  }
  if (not has_count) {
    return GLTF_ERROR_MISSING_FIELD("count");
  }
  if (not has_type) {
    return GLTF_ERROR_MISSING_FIELD("type");
  }

  return accessor;
}

static Result<GltfBufferView, GltfErrorInfo>
gltf_parse_buffer_view(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);
  GltfBufferView view;
  bool has_length = false;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "name") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(view.name, value, JsonType::String);
      view.name = view.name.copy(ctx->arena);
    } else if (key == "buffer") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(view.buffer, value, JsonType::Integer);
      if (view.buffer < 0) {
        return GLTF_ERROR(GltfError::InvalidFormat, "Invalid value {}",
                          view.buffer);
      }
    } else if (key == "byteOffset") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(view.byte_offset, value,
                                     JsonType::Integer);
    } else if (key == "byteLength") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(view.byte_length, value,
                                     JsonType::Integer);
      has_length = true;
    } else if (key == "byteStride") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(view.byte_stride, value,
                                     JsonType::Integer);
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  if (view.buffer == -1) {
    return GLTF_ERROR_MISSING_FIELD("bufferView");
  }
  if (not has_length) {
    return GLTF_ERROR_MISSING_FIELD("byteLength");
  }
  return view;
}

static Result<GltfBuffer, GltfErrorInfo>
gltf_parse_buffer(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);
  GltfBuffer buffer;
  bool has_length = false;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "name") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(buffer.name, value, JsonType::String);
      buffer.name = buffer.name.copy(ctx->arena);
    } else if (key == "uri") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(buffer.uri, value, JsonType::String);
      buffer.uri = buffer.uri.copy(ctx->arena);
    } else if (key == "byteLength") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(buffer.byte_length, value,
                                     JsonType::Integer);
      has_length = true;
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  if (not has_length) {
    return GLTF_ERROR_MISSING_FIELD("byteLength");
  }
  return buffer;
}

template <typename T>
static Result<Span<T>, GltfErrorInfo>
gltf_parse_array(NotNull<GltfParserContext *> ctx, JsonValue json) {
  GLTF_JSON_CAST_OR_RETURN_ERROR(auto json_array, json, JsonType::Array);
  auto array = Span<T>::allocate(ctx->arena, json_array.size());
  for (usize i : range(array.size())) {
    GLTF_JSON_PATH_SCOPE(i);
    Result<T, GltfErrorInfo> parse_result = [&]() {
      if constexpr (std::is_same_v<T, GltfScene>) {
        return gltf_parse_scene(ctx, json_array[i]);
      } else if constexpr (std::is_same_v<T, GltfNode>) {
        return gltf_parse_node(ctx, json_array[i]);
      } else if constexpr (std::is_same_v<T, GltfMesh>) {
        return gltf_parse_mesh(ctx, json_array[i]);
      } else if constexpr (std::is_same_v<T, GltfImage>) {
        return gltf_parse_image(ctx, json_array[i]);
      } else if constexpr (std::is_same_v<T, GltfAccessor>) {
        return gltf_parse_accessor(ctx, json_array[i]);
      } else if constexpr (std::is_same_v<T, GltfBufferView>) {
        return gltf_parse_buffer_view(ctx, json_array[i]);
      } else if constexpr (std::is_same_v<T, GltfBuffer>) {
        return gltf_parse_buffer(ctx, json_array[i]);
      } else if constexpr (std::is_same_v<T, GltfMaterial>) {
        return gltf_parse_material(ctx, json_array[i]);
      } else if constexpr (std::is_same_v<T, GltfTexture>) {
        return gltf_parse_texture(ctx, json_array[i]);
      } else if constexpr (std::is_same_v<T, GltfSampler>) {
        return gltf_parse_sampler(ctx, json_array[i]);
      } else {
        static_assert(false);
      }
    }();
    if (!parse_result) {
      return parse_result.error();
    }
    array[i] = *parse_result;
  }
  return array;
}

Result<Gltf, GltfErrorInfo> gltf_parse(NotNull<GltfParserContext *> ctx,
                                       JsonValue json) {
  ZoneScoped;
  GLTF_JSON_VERIFY_CASTABLE_OR_RETURN_ERROR(json, JsonType::Object);
  Gltf gltf;
  bool has_asset = false;
  for (auto [key, value] : json_object(json)) {
    GLTF_JSON_PATH_SCOPE(key);
    if (key == "asset") {
      GLTF_TRY(gltf.asset, gltf_parse_asset(ctx, value));
      has_asset = true;
    } else if (key == "scene") {
      GLTF_JSON_CAST_OR_RETURN_ERROR(gltf.scene, value, JsonType::Integer);
    } else if (key == "scenes") {
      GLTF_TRY(gltf.scenes, gltf_parse_array<GltfScene>(ctx, value));
    } else if (key == "nodes") {
      GLTF_TRY(gltf.nodes, gltf_parse_array<GltfNode>(ctx, value));
    } else if (key == "meshes") {
      GLTF_TRY(gltf.meshes, gltf_parse_array<GltfMesh>(ctx, value));
    } else if (key == "images") {
      GLTF_TRY(gltf.images, gltf_parse_array<GltfImage>(ctx, value));
    } else if (key == "accessors") {
      GLTF_TRY(gltf.accessors, gltf_parse_array<GltfAccessor>(ctx, value));
    } else if (key == "bufferViews") {
      GLTF_TRY(gltf.buffer_views, gltf_parse_array<GltfBufferView>(ctx, value));
    } else if (key == "buffers") {
      GLTF_TRY(gltf.buffers, gltf_parse_array<GltfBuffer>(ctx, value));
    } else if (key == "materials") {
      GLTF_TRY(gltf.materials, gltf_parse_array<GltfMaterial>(ctx, value));
    } else if (key == "textures") {
      GLTF_TRY(gltf.textures, gltf_parse_array<GltfTexture>(ctx, value));
    } else if (key == "samplers") {
      GLTF_TRY(gltf.samplers, gltf_parse_array<GltfSampler>(ctx, value));
    } else {
      GLTF_WARN_IGNORED;
    }
  }
  if (not has_asset) {
    return GLTF_ERROR_MISSING_FIELD("asset");
  }
  return gltf;
}

Result<Gltf, GltfErrorInfo> load_gltf(NotNull<Arena *> arena,
                                      const GltfLoadInfo &load_info) {
  ZoneScoped;
  ScratchArena scratch;
  IoResult<Span<char>> buffer = read<char>(scratch, load_info.path);
  if (!buffer) {
    return GltfErrorInfo{
        .error = GltfError::IO,
        .message = format(arena, "Failed to read {}: {}", load_info.path,
                          buffer.error()),
    };
  }
  Result<JsonValue, JsonErrorInfo> json =
      json_parse(scratch, {buffer->data(), buffer->size()});
  if (!json) {
    JsonErrorInfo error_info = json.error();
    return GltfErrorInfo{
        .error = GltfError::JSON,
        .message = format(arena, "{}:{}:{}: {}", load_info.path,
                          error_info.line, error_info.column, error_info.error),
    };
  }
  GltfParserContext ctx = {
      .arena = arena,
      .scratch = scratch,
  };
  Result<Gltf, GltfErrorInfo> gltf = gltf_parse(&ctx, *json);
  if (!gltf) {
    return gltf.error();
  }
  if (load_info.load_buffers) {
    Result<void, GltfErrorInfo> buffer_load_result =
        gltf_load_buffers(arena, &*gltf, load_info.path);
    if (!buffer_load_result) {
      return buffer_load_result.error();
    }
  }
  if (load_info.load_images) {
    ren_assert(load_info.load_image_callback);
    Result<void, GltfErrorInfo> image_load_result = gltf_load_images(
        arena, &*gltf, load_info.path, load_info.load_image_callback,
        load_info.load_image_context);
    if (!image_load_result) {
      return image_load_result.error();
    }
  }
  if (load_info.optimize_flags != EmptyFlags) {
    gltf_optimize(arena, &*gltf, load_info.optimize_flags);
  }
  return *gltf;
}

Result<void, GltfErrorInfo> gltf_load_buffers(NotNull<Arena *> arena,
                                              NotNull<Gltf *> gltf,
                                              Path gltf_path) {
  ScratchArena scratch;
  Path parent_path = gltf_path.parent();
  for (GltfBuffer &buffer : gltf->buffers) {
    Path path = parent_path.concat(scratch, Path::init(scratch, buffer.uri));
    IoResult<Span<std::byte>> bytes = read<std::byte>(arena, path);
    if (!bytes) {
      return GltfErrorInfo{
          .error = GltfError::IO,
          .message = format(
              arena, "Failed to load GLTF buffers: Failed to read {}: {}", path,
              bytes.error()),
      };
    }
    buffer.bytes = *bytes;
  }
  return {};
}

Result<void, GltfErrorInfo>
gltf_load_images(NotNull<Arena *> arena, NotNull<Gltf *> gltf, Path gltf_path,
                 GltfLoadImageCallback cb, void *context) {
  ScratchArena scratch;
  Path parent_path = gltf_path.parent();
  for (usize image_index : range(gltf->images.size())) {
    GltfImage &image = gltf->images[image_index];
    Span<const std::byte> bytes;
    if (image.uri) {
      Path path = parent_path.concat(scratch, Path::init(scratch, image.uri));
      IoResult<Span<std::byte>> read_result = read<std::byte>(scratch, path);
      if (!read_result) {
        return GltfErrorInfo{
            .error = GltfError::IO,
            .message = format(
                arena, "Failed to load GLTF images: Failed to read {}: {}",
                path, read_result.error()),
        };
      }
      bytes = *read_result;
    } else {
      ren_assert(image.buffer_view != -1);
      const GltfBufferView &view = gltf->buffer_views[image.buffer_view];
      bytes = gltf->buffers[view.buffer].bytes.subspan(view.byte_offset,
                                                       view.byte_length);
    }
    Result<GltfLoadedImage, GltfLoadImageErrorInfo> loaded_image =
        cb(arena, context, bytes);
    if (!loaded_image) {
      String8 name = image.uri;
      if (!name) {
        name = image.name;
      }
      if (!name) {
        name = format(scratch, "image {}", image_index);
      }
      return GltfErrorInfo{
          .error = GltfError::IO,
          .message = format(
              arena, "Failed to load GLTF images: Failed to decode {}: {}",
              name, loaded_image.error().message),
      };
    }
    image.pixels = loaded_image->pixels;
    image.width = loaded_image->width;
    image.height = loaded_image->height;
  }
  return {};
}

template <typename T>
static JsonValue gltf_serialize_array(NotNull<Arena *> arena, Span<T> array) {
  Span<JsonValue> json = Span<JsonValue>::allocate(arena, array.size());
  for (usize i : range(array.size())) {
    json[i] = gltf_serialize(arena, array[i]);
  }
  return JsonValue::init(json);
}

template <int L>
static JsonValue gltf_serialize(NotNull<Arena *> arena, glm::vec<L, float> v) {
  auto json = Span<JsonValue>::allocate(arena, L);
  for (usize i : range(L)) {
    json[i] = JsonValue::from_float(v[i]);
  }
  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfAsset &asset) {
  DynamicArray<JsonKeyValue> json;
  json.push(arena, {"version", JsonValue::from_string("2.0")});
  if (asset.generator) {
    json.push(arena, {"generator", JsonValue::from_string("ren GLTF")});
  }
  if (asset.copyright) {
    json.push(arena,
              {"copyright", JsonValue::from_string(arena, asset.copyright)});
  }
  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfScene &scene) {
  DynamicArray<JsonKeyValue> json;
  if (scene.name) {
    json.push(arena, {"name", JsonValue::from_string(arena, scene.name)});
  }
  if (scene.nodes.m_size > 0) {
    Span<JsonValue> nodes =
        Span<JsonValue>::allocate(arena, scene.nodes.size());
    for (usize i : range(nodes.size())) {
      nodes[i] = JsonValue::from_integer(scene.nodes[i]);
    }
    json.push(arena, {"nodes", JsonValue::init(nodes)});
  }
  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena, GltfNode node) {
  DynamicArray<JsonKeyValue> json;

  if (node.name) {
    json.push(arena, {"name", JsonValue::from_string(arena, node.name)});
  }

  if (node.mesh >= 0) {
    json.push(arena, {"mesh", JsonValue::from_integer(node.mesh)});
  }

  glm::mat4 transform = glm::identity<glm::mat4>();
  transform = glm::translate(transform, node.translation);
  transform = transform * glm::mat4(node.rotation);
  transform = glm::scale(transform, node.scale);
  if (node.matrix == transform) {
    node.matrix = glm::identity<glm::mat4>();
  } else {
    glm::vec3 scale;
    glm::quat rotation;
    glm::vec3 translation;
    glm::vec3 skew;
    glm::vec4 perspective;
    if (glm::decompose(transform, scale, rotation, translation, skew,
                       perspective)) {
      if (perspective == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) and
          glm::dot(skew, skew) < 0.001f) {
        node.matrix = glm::identity<glm::mat4>();
        node.translation = translation;
        node.rotation = rotation;
        node.scale = scale;
      }
    }
  }

  if (node.matrix != glm::identity<glm::mat4>()) {
    auto matrix = Span<JsonValue>::allocate(arena, 16);
    for (usize i : range(16)) {
      matrix[i] = JsonValue::from_float(glm::value_ptr(node.matrix)[i]);
    }
    json.push(arena, {"matrix", JsonValue::init(matrix)});
  } else {
    auto translation = Span<JsonValue>::allocate(arena, 3);
    for (usize i : range(3)) {
      translation[i] = JsonValue::from_float(node.translation[i]);
    }
    json.push(arena, {"translation", JsonValue::init(translation)});

    auto rotation = Span<JsonValue>::allocate(arena, 4);
    rotation[0] = JsonValue::from_float(node.rotation.x);
    rotation[1] = JsonValue::from_float(node.rotation.y);
    rotation[2] = JsonValue::from_float(node.rotation.z);
    rotation[3] = JsonValue::from_float(node.rotation.w);
    json.push(arena, {"rotation", JsonValue::init(rotation)});

    auto scale = Span<JsonValue>::allocate(arena, 3);
    for (usize i : range(3)) {
      scale[i] = JsonValue::from_float(node.scale[i]);
    }
    json.push(arena, {"scale", JsonValue::init(scale)});
  }

  if (node.children.m_size > 0) {
    auto children = Span<JsonValue>::allocate(arena, node.children.size());
    for (usize i : range(children.size())) {
      children[i] = JsonValue::from_integer(node.children[i]);
    }
    json.push(arena, {"children", JsonValue::init(children)});
  }

  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfPrimitive &primitive) {
  DynamicArray<JsonKeyValue> json;

  auto attributes =
      Span<JsonKeyValue>::allocate(arena, primitive.attributes.size());
  for (usize i : range(attributes.size())) {
    GltfAttribute attribute = primitive.attributes[i];
    String8 name;
    switch (attribute.semantic) {
    case GltfAttributeSemantic::POSITION:
    case GltfAttributeSemantic::NORMAL:
    case GltfAttributeSemantic::TANGENT:
      name = format(arena, "{}", attribute.semantic);
      break;
    case GltfAttributeSemantic::TEXCOORD:
    case GltfAttributeSemantic::COLOR:
    case GltfAttributeSemantic::JOINTS:
    case GltfAttributeSemantic::WEIGHTS:
      name = format(arena, "{}_{}", attribute.semantic, attribute.set_index);
      break;
    case GltfAttributeSemantic::USER:
      name = attribute.name.copy(arena);
      break;
    }
    attributes[i] = {
        name,
        JsonValue::from_integer(primitive.attributes[i].accessor),
    };
  }
  json.push(arena, {"attributes", JsonValue::init(attributes)});

  if (primitive.indices >= 0) {
    json.push(arena, {"indices", JsonValue::from_integer(primitive.indices)});
  }

  json.push(arena, {"mode", JsonValue::from_integer(primitive.mode)});

  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena, const GltfMesh &mesh) {
  DynamicArray<JsonKeyValue> json;

  if (mesh.name) {
    json.push(arena, {"name", JsonValue::from_string(arena, mesh.name)});
  }

  auto primitives = Span<JsonValue>::allocate(arena, mesh.primitives.size());
  for (usize i : range(primitives.size())) {
    primitives[i] = gltf_serialize(arena, mesh.primitives[i]);
  }
  json.push(arena,
            {"primitives", gltf_serialize_array(arena, mesh.primitives)});

  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfImage &image) {
  DynamicArray<JsonKeyValue> json;

  if (image.name) {
    json.push(arena, {"name", JsonValue::from_string(arena, image.name)});
  }

  if (image.buffer_view >= 0) {
    json.push(arena,
              {"bufferView", JsonValue::from_integer(image.buffer_view)});
  }

  if (image.mime_type) {
    json.push(arena,
              {"mimeType", JsonValue::from_string(arena, image.mime_type)});
  }

  if (image.uri) {
    json.push(arena, {"uri", JsonValue::from_string(arena, image.uri)});
  }

  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfAccessor &accessor) {
  DynamicArray<JsonKeyValue> json;

  if (accessor.name) {
    json.push(arena, {"name", JsonValue::from_string(arena, accessor.name)});
  }

  if (accessor.buffer_view >= 0) {
    json.push(arena,
              {"bufferView", JsonValue::from_integer(accessor.buffer_view)});
  }

  if (accessor.byte_offset > 0) {
    json.push(arena,
              {"byteOffset", JsonValue::from_integer(accessor.byte_offset)});
  }

  json.push(arena, {"componentType",
                    JsonValue::from_integer(accessor.component_type)});

  json.push(arena,
            {"normalized", JsonValue::from_boolean(accessor.normalized)});

  json.push(arena, {"count", JsonValue::from_integer(accessor.count)});

  String8 ACCESSOR_TYPE_MAP[GLTF_ACCESSOR_TYPE_MAT4 + 1];
  ACCESSOR_TYPE_MAP[GLTF_ACCESSOR_TYPE_SCALAR] = "SCALAR";
  ACCESSOR_TYPE_MAP[GLTF_ACCESSOR_TYPE_VEC2] = "VEC2";
  ACCESSOR_TYPE_MAP[GLTF_ACCESSOR_TYPE_VEC3] = "VEC3";
  ACCESSOR_TYPE_MAP[GLTF_ACCESSOR_TYPE_VEC4] = "VEC4";
  ACCESSOR_TYPE_MAP[GLTF_ACCESSOR_TYPE_MAT2] = "MAT2";
  ACCESSOR_TYPE_MAP[GLTF_ACCESSOR_TYPE_MAT3] = "MAT3";
  ACCESSOR_TYPE_MAP[GLTF_ACCESSOR_TYPE_MAT4] = "MAT4";
  json.push(arena, {"type", JsonValue::from_string(
                                arena, ACCESSOR_TYPE_MAP[accessor.type])});

  return JsonValue::init(json);
}
static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfBufferView &view) {
  DynamicArray<JsonKeyValue> json;

  if (view.name) {
    json.push(arena, {"name", JsonValue::from_string(arena, view.name)});
  }

  json.push(arena, {"buffer", JsonValue::from_integer(view.buffer)});

  if (view.byte_offset > 0) {
    json.push(arena, {"byteOffset", JsonValue::from_integer(view.byte_offset)});
  }

  json.push(arena, {"byteLength", JsonValue::from_integer(view.byte_length)});

  if (view.byte_stride > 0) {
    json.push(arena, {"byteStride", JsonValue::from_integer(view.byte_stride)});
  }

  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfBuffer &buffer) {
  DynamicArray<JsonKeyValue> json;

  if (buffer.name) {
    json.push(arena, {"name", JsonValue::from_string(arena, buffer.name)});
  }

  if (buffer.uri) {
    json.push(arena, {"uri", JsonValue::from_string(arena, buffer.uri)});
  }

  json.push(arena, {"byteLength", JsonValue::from_integer(buffer.byte_length)});

  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfTextureInfo &texture_info) {
  DynamicArray<JsonKeyValue> json;
  json.reserve(arena, 2);
  json.push({"index", JsonValue::from_integer(texture_info.index)});
  json.push({"texCoord", JsonValue::from_integer(texture_info.tex_coord)});
  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfNormalTextureInfo &texture_info) {
  DynamicArray<JsonKeyValue> json;
  json.reserve(arena, 3);
  json.push({"index", JsonValue::from_integer(texture_info.index)});
  json.push({"texCoord", JsonValue::from_integer(texture_info.tex_coord)});
  if (texture_info.scale != 1.0f) {
    json.push({"scale", JsonValue::from_float(texture_info.scale)});
  }
  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfOcclusionTextureInfo &texture_info) {
  DynamicArray<JsonKeyValue> json;
  json.reserve(arena, 3);
  json.push({"index", JsonValue::from_integer(texture_info.index)});
  json.push({"texCoord", JsonValue::from_integer(texture_info.tex_coord)});
  if (texture_info.strength != 1.0f) {
    json.push({"strength", JsonValue::from_float(texture_info.strength)});
  }
  return JsonValue::init(json);
}

static JsonValue
gltf_serialize(NotNull<Arena *> arena,
               const GltfPbrMetallicRoughness &pbr_metallic_roughness) {
  DynamicArray<JsonKeyValue> json;
  json.reserve(arena, 5);
  json.push({"baseColorFactor",
             gltf_serialize(arena, pbr_metallic_roughness.base_color_factor)});
  if (pbr_metallic_roughness.base_color_texture.index != -1) {
    json.push(
        {"baseColorTexture",
         gltf_serialize(arena, pbr_metallic_roughness.base_color_texture)});
  }
  json.push({"metallicFactor",
             JsonValue::from_float(pbr_metallic_roughness.metallic_factor)});
  json.push({"roughnessFactor",
             JsonValue::from_float(pbr_metallic_roughness.roughness_factor)});
  if (pbr_metallic_roughness.metallic_roughness_texture.index != -1) {
    json.push({"metallicRoughnessTexture",
               gltf_serialize(
                   arena, pbr_metallic_roughness.metallic_roughness_texture)});
  }
  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                GltfAlphaMode alpha_mode) {
  switch (alpha_mode) {
  case GLTF_ALPHA_MODE_OPAQUE:
    return JsonValue::from_string("OPAQUE");
  case GLTF_ALPHA_MODE_MASK:
    return JsonValue::from_string("MASK");
  case GLTF_ALPHA_MODE_BLEND:
    return JsonValue::from_string("BLEND");
  }
  unreachable();
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfMaterial &material) {
  DynamicArray<JsonKeyValue> json;
  if (material.name) {
    json.push(arena, {"name", JsonValue::from_string(arena, material.name)});
  }
  json.push(arena, {"pbrMetallicRoughness",
                    gltf_serialize(arena, material.pbr_metallic_roughness)});
  if (material.normal_texture.index != -1) {
    json.push(arena, {"normalTexture",
                      gltf_serialize(arena, material.normal_texture)});
  }
  if (material.occlusion_texture.index != -1) {
    json.push(arena, {"occlusionTexture",
                      gltf_serialize(arena, material.occlusion_texture)});
  }
  if (material.emissive_texture.index != -1) {
    json.push(arena, {"emissiveTexture",
                      gltf_serialize(arena, material.emissive_texture)});
  }
  if (material.emissive_factor != glm::vec3{0.0f, 0.0f, 0.0f}) {
    json.push(arena, {"emissiveFactor",
                      gltf_serialize(arena, material.emissive_factor)});
  }
  if (material.alphaMode != GLTF_ALPHA_MODE_OPAQUE) {
    json.push(arena, {"alphaMode", gltf_serialize(arena, material.alphaMode)});
  }
  if (material.alphaMode == GLTF_ALPHA_MODE_MASK) {
    json.push(arena,
              {"alphaCutoff", JsonValue::from_float(material.alphaCutoff)});
  }
  if (material.doubleSided) {
    json.push(arena, {"doubleSided", JsonValue::from_boolean(true)});
  }
  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfSampler &sampler) {

  DynamicArray<JsonKeyValue> json;
  json.reserve(arena, 5);
  if (sampler.name) {
    json.push({"name", JsonValue::from_string(arena, sampler.name)});
  }
  json.push({"wrapS", JsonValue::from_integer(sampler.wrap_s)});
  json.push({"wrapT", JsonValue::from_integer(sampler.wrap_t)});
  json.push({"magFilter", JsonValue::from_integer(sampler.mag_filter)});
  json.push({"minFilter", JsonValue::from_integer(sampler.min_filter)});
  return JsonValue::init(json);
}

static JsonValue gltf_serialize(NotNull<Arena *> arena,
                                const GltfTexture &texture) {
  DynamicArray<JsonKeyValue> json;
  json.reserve(arena, 2);
  json.push({"source", JsonValue::from_integer(texture.source)});
  json.push({"sampler", JsonValue::from_integer(texture.sampler)});
  return JsonValue::init(json);
}

JsonValue to_json(NotNull<Arena *> arena, const Gltf &gltf) {
  DynamicArray<JsonKeyValue> json;

  json.push(arena, {"asset", gltf_serialize(arena, gltf.asset)});

  if (gltf.scene != -1) {
    json.push(arena, {"scene", JsonValue::from_integer(gltf.scene)});
  }

  if (gltf.scenes.m_size > 0) {
    json.push(arena, {"scenes", gltf_serialize_array(arena, gltf.scenes)});
  }

  if (gltf.nodes.m_size > 0) {
    json.push(arena, {"nodes", gltf_serialize_array(arena, gltf.nodes)});
  }

  if (gltf.meshes.m_size > 0) {
    json.push(arena, {"meshes", gltf_serialize_array(arena, gltf.meshes)});
  }

  if (not gltf.materials.is_empty()) {
    json.push(arena,
              {"materials", gltf_serialize_array(arena, gltf.materials)});
  }

  if (not gltf.samplers.is_empty()) {
    json.push(arena, {"samplers", gltf_serialize_array(arena, gltf.samplers)});
  }

  if (not gltf.textures.is_empty()) {
    json.push(arena, {"textures", gltf_serialize_array(arena, gltf.textures)});
  }

  if (not gltf.images.is_empty()) {
    json.push(arena, {"images", gltf_serialize_array(arena, gltf.images)});
  }

  if (gltf.accessors.m_size > 0) {
    json.push(arena,
              {"accessors", gltf_serialize_array(arena, gltf.accessors)});
  }

  if (gltf.buffer_views.m_size > 0) {
    json.push(arena,
              {"bufferViews", gltf_serialize_array(arena, gltf.buffer_views)});
  }

  if (gltf.buffers.m_size > 0) {
    json.push(arena, {"buffers", gltf_serialize_array(arena, gltf.buffers)});
  }

  return JsonValue::init(Span(json));
}

String8 gltf_serialize(NotNull<Arena *> arena, const Gltf &gltf) {
  ZoneScoped;
  ScratchArena scratch;
  JsonValue json = to_json(scratch, gltf);
  return json_serialize(arena, json);
}

void gltf_optimize(NotNull<Arena *> arena, NotNull<Gltf *> gltf,
                   GltfOptimizeFlags flags) {
  ZoneScoped;

  ScratchArena scratch;

  Span<GltfNode> nodes = gltf->nodes.copy(scratch);
  Span<GltfScene> scenes = gltf->scenes.copy(scratch);

  for (GltfNode &node : nodes) {
    node.camera = flags.is_set(GltfOptimize::RemoveCameras) ? -1 : node.camera;
    node.skin = flags.is_set(GltfOptimize::RemoveSkins) ? -1 : node.skin;
  }

  if (flags.is_set(GltfOptimize::RemoveRedundantNodes)) {
    Span<i32> node_parents = Span<i32>::allocate(scratch, nodes.size());
    fill(node_parents, -1);
    for (usize node_index : range(nodes.size())) {
      const GltfNode &node = nodes[node_index];
      for (i32 child : node.children) {
        node_parents[child] = node_index;
      }
    }

    Span<bool> keep_node = Span<bool>::allocate(scratch, nodes.size());

    // Discover unreferenced nodes.
    DynamicArray<i32> stack;
    stack.reserve(scratch, nodes.size());
    for (const GltfScene &scene : scenes) {
      for (i32 node_index : scene.nodes) {
        stack.push(scratch, node_index);
      }
    }
    while (not stack.is_empty()) {
      i32 node_index = stack.pop();
      keep_node[node_index] = true;
      for (i32 child : nodes[node_index].children) {
        stack.push(child);
      }
    }

    // TODO(mbargatin): delete dangling nodes that don't affect the hierarchy in
    // any meaningful way.

    Span<i32> node_remap = Span<i32>::allocate(scratch, nodes.size());
    DynamicArray<GltfNode> remapped_nodes;
    remapped_nodes.reserve(scratch, nodes.size());
    for (usize node_index : range(nodes.size())) {
      if (keep_node[node_index]) {
        i32 remapped_node_index = remapped_nodes.size();
        remapped_nodes.push(nodes[node_index]);
        node_remap[node_index] = remapped_node_index;
      }
    }

    for (GltfScene &scene : scenes) {
      DynamicArray<i32> scene_nodes;
      scene_nodes.reserve(scratch, scene.nodes.size());
      for (i32 node_index : scene.nodes) {
        if (keep_node[node_index]) {
          scene_nodes.push(node_remap[node_index]);
        }
      }
      scene.nodes = scene_nodes;
    }

    for (GltfNode &node : remapped_nodes) {
      DynamicArray<i32> children;
      children.reserve(scratch, node.children.size());
      for (i32 child_node_index : node.children) {
        if (keep_node[child_node_index]) {
          children.push(node_remap[child_node_index]);
        }
      }
      node.children = children;
    }

    nodes = remapped_nodes;
  }

  if (flags.is_set(GltfOptimize::RemoveEmptyScenes)) {
    DynamicArray<GltfScene> non_empty_scenes;
    non_empty_scenes.reserve(scratch, scenes.size());
    for (const GltfScene &scene : scenes) {
      if (not scene.nodes.is_empty()) {
        non_empty_scenes.push(scene);
      }
    }
    scenes = non_empty_scenes;
  }

  Span<GltfMesh> meshes = gltf->meshes.copy(scratch);

  Span<GltfMaterial> materials;
  if (not flags.is_set(GltfOptimize::RemoveMaterials)) {
    materials = gltf->materials.copy(scratch);
  }

  Span<const GltfImage> images;
  if (not flags.is_set(GltfOptimize::RemoveImages)) {
    images = gltf->images;
  }

  Span<const GltfSampler> samplers;
  Span<const GltfTexture> textures;
  if (flags.is_none_set(GltfOptimize::RemoveMaterials |
                        GltfOptimize::RemoveMaterials)) {
    samplers = gltf->samplers;
    textures = gltf->textures;
  }

  if (flags.is_set(GltfOptimize::RemoveMaterials)) {
    for (GltfMesh &mesh : meshes) {
      for (GltfPrimitive &primitive : mesh.primitives) {
        primitive.material = -1;
      }
    }
  }

  if (flags.is_set(GltfOptimize::RemoveImages)) {
    for (GltfMaterial &material : materials) {
      material.pbr_metallic_roughness.base_color_texture = {};
      material.pbr_metallic_roughness.metallic_roughness_texture = {};
      material.normal_texture = {};
      material.occlusion_texture = {};
    }
  }

  if (flags.is_set(GltfOptimize::RemoveRedundantMeshes)) {
    Span<bool> keep_mesh = Span<bool>::allocate(scratch, meshes.size());
    for (const GltfNode &node : nodes) {
      if (node.mesh != -1) {
        keep_mesh[node.mesh] = true;
      }
    }

    Span<i32> mesh_mapping = Span<i32>::allocate(scratch, meshes.size());

    DynamicArray<i32> unique_mesh_indices;
    unique_mesh_indices.reserve(scratch, meshes.m_size);

    for (usize mesh_index : range(meshes.size())) {
      if (not keep_mesh[mesh_index]) {
        continue;
      }
      const GltfMesh &mesh = meshes[mesh_index];

      i32 found_index = -1;
      for (usize j = 0; j < unique_mesh_indices.m_size; ++j) {
        const GltfMesh &unique_mesh = meshes[unique_mesh_indices[j]];

        if (mesh.primitives.m_size == unique_mesh.primitives.m_size) {
          bool all_match = true;
          for (usize p = 0; p < mesh.primitives.m_size; ++p) {
            if (mesh.primitives[p] != unique_mesh.primitives[p]) {
              all_match = false;
              break;
            }
          }
          if (all_match) {
            found_index = (i32)j;
            break;
          }
        }
      }

      if (found_index == -1) {
        found_index = unique_mesh_indices.m_size;
        unique_mesh_indices.push(mesh_index);
      }
      mesh_mapping[mesh_index] = found_index;
    }

    for (GltfNode &node : nodes) {
      node.mesh = node.mesh == -1 ? -1 : mesh_mapping[node.mesh];
    }

    auto remapped_meshes =
        Span<GltfMesh>::allocate(scratch, unique_mesh_indices.size());
    for (usize mesh_index : range(remapped_meshes.size())) {
      remapped_meshes[mesh_index] = meshes[unique_mesh_indices[mesh_index]];
    }
    meshes = remapped_meshes;
  }

  DynamicArray<GltfBufferView> buffer_views;
  DynamicArray<GltfAccessor> accessors;
  DynamicArray<GltfAccessor> src_accessors;

  auto remap_mesh_accessor = [&](usize src_accessor_index,
                                 Optional<GltfAttributeSemantic> semantic) {
    const GltfAccessor &accessor = gltf->accessors[src_accessor_index];

    GltfComponentType component_type = accessor.component_type;
    bool normalized = accessor.normalized;
    u32 count = accessor.count;
    GltfAccessorType accessor_type = accessor.type;

    i32 buffer_view_index = buffer_views.size();
    i32 accessor_index = accessors.size();

    if (flags.is_set(GltfOptimize::ConvertMeshAccessors)) {
      if (semantic) {
        // TODO(mbargatin): need to verify semantics when loading the GLTF file.
        switch (*semantic) {
        case GltfAttributeSemantic::POSITION:
          ren_assert(accessor_type == GLTF_ACCESSOR_TYPE_VEC3);
          ren_assert(component_type == GLTF_COMPONENT_TYPE_FLOAT);
          break;
        case GltfAttributeSemantic::NORMAL:
          ren_assert(accessor_type == GLTF_ACCESSOR_TYPE_VEC3);
          ren_assert(component_type == GLTF_COMPONENT_TYPE_FLOAT);
          break;
        case GltfAttributeSemantic::TANGENT:
          ren_assert(accessor_type == GLTF_ACCESSOR_TYPE_VEC4);
          ren_assert(component_type == GLTF_COMPONENT_TYPE_FLOAT);
          break;
        case GltfAttributeSemantic::TEXCOORD:
          ren_assert(accessor_type == GLTF_ACCESSOR_TYPE_VEC2);
          ren_assert(component_type == GLTF_COMPONENT_TYPE_FLOAT or
                     (component_type == GLTF_COMPONENT_TYPE_UNSIGNED_BYTE and
                      normalized) or
                     (component_type == GLTF_COMPONENT_TYPE_UNSIGNED_SHORT and
                      normalized));
          component_type = GLTF_COMPONENT_TYPE_FLOAT;
          normalized = false;
          break;
        case GltfAttributeSemantic::COLOR:
          ren_assert(accessor_type == GLTF_ACCESSOR_TYPE_VEC3 or
                     accessor_type == GLTF_ACCESSOR_TYPE_VEC4);
          ren_assert(component_type == GLTF_COMPONENT_TYPE_FLOAT or
                     (component_type == GLTF_COMPONENT_TYPE_UNSIGNED_BYTE and
                      normalized) or
                     (component_type == GLTF_COMPONENT_TYPE_UNSIGNED_SHORT and
                      normalized));
          accessor_type = GLTF_ACCESSOR_TYPE_VEC4;
          component_type = GLTF_COMPONENT_TYPE_FLOAT;
          normalized = false;
          break;
        case GltfAttributeSemantic::JOINTS:
          ren_assert(accessor_type == GLTF_ACCESSOR_TYPE_VEC4);
          ren_assert(component_type == GLTF_COMPONENT_TYPE_UNSIGNED_BYTE or
                     component_type == GLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
          break;
        case GltfAttributeSemantic::WEIGHTS:
          ren_assert(component_type == GLTF_COMPONENT_TYPE_FLOAT or
                     (component_type == GLTF_COMPONENT_TYPE_UNSIGNED_BYTE and
                      normalized) or
                     (component_type == GLTF_COMPONENT_TYPE_UNSIGNED_SHORT and
                      normalized));
          break;
        case GltfAttributeSemantic::USER:
          break;
        }
      } else {
        ren_assert(accessor_type == GLTF_ACCESSOR_TYPE_SCALAR);
        ren_assert(component_type == GLTF_COMPONENT_TYPE_UNSIGNED_BYTE or
                   component_type == GLTF_COMPONENT_TYPE_UNSIGNED_SHORT or
                   component_type == GLTF_COMPONENT_TYPE_UNSIGNED_INT);
        ren_assert(not normalized);
        accessor_type = GLTF_ACCESSOR_TYPE_SCALAR;
        component_type = GLTF_COMPONENT_TYPE_UNSIGNED_INT;
      }
      src_accessors.push(scratch, accessor);
    }

    usize stride = gltf_accessor_packed_stride(accessor_type, component_type);
    usize size = stride * accessor.count;

    buffer_views.push(
        scratch, {
                     .name = format(arena, "Buffer View {}", buffer_view_index),
                     .buffer = 0,
                     .byte_length = (u32)size,
                 });

    accessors.push(scratch,
                   {
                       .name = format(arena, "Accessor {}", accessor_index),
                       .buffer_view = buffer_view_index,
                       .byte_offset = 0,
                       .component_type = component_type,
                       .normalized = normalized,
                       .count = count,
                       .type = accessor_type,
                   });

    return accessor_index;
  };

  struct RemappedAttributeAccessor {
    GltfAttributeSemantic semantic = {};
    i32 src_accessor = -1;
    i32 remapped_accessor = -1;
  };

  DynamicArray<RemappedAttributeAccessor> attribute_accessor_mapping;

  Span<i32> index_accessor_mapping =
      Span<i32>::allocate(scratch, gltf->accessors.size());
  fill(index_accessor_mapping, -1);

  for (usize mesh_index : range(meshes.size())) {
    GltfMesh &mesh = meshes[mesh_index];
    mesh.primitives = mesh.primitives.copy(arena);

    for (usize primitive_index : range(mesh.primitives.size())) {
      GltfPrimitive &primitive = mesh.primitives[primitive_index];
      primitive.attributes = primitive.attributes.copy(arena);

      for (usize attribute_index : range(primitive.attributes.size())) {
        GltfAttribute &attribute = primitive.attributes[attribute_index];
        i32 src_accessor_index = attribute.accessor;
        i32 accessor_index = -1;
        for (usize i : range(attribute_accessor_mapping.size())) {
          if (attribute_accessor_mapping[i].semantic == attribute.semantic and
              attribute_accessor_mapping[i].src_accessor ==
                  src_accessor_index) {
            accessor_index = attribute_accessor_mapping[i].remapped_accessor;
            break;
          }
        }
        if (accessor_index == -1) {
          accessor_index =
              remap_mesh_accessor(attribute.accessor, attribute.semantic);
          attribute_accessor_mapping.push(
              scratch,
              {attribute.semantic, src_accessor_index, accessor_index});
        }
        primitive.attributes[attribute_index].accessor = accessor_index;
      }

      if (primitive.indices != -1) {
        i32 src_accessor_index = primitive.indices;
        i32 accessor_index = index_accessor_mapping[src_accessor_index];
        if (accessor_index == -1) {
          accessor_index = remap_mesh_accessor(primitive.indices, {});
          index_accessor_mapping[src_accessor_index] = accessor_index;
        }
        primitive.indices = accessor_index;
      }
    }
  }

  usize blob_size = 0;
  for (const GltfBufferView &buffer_view : buffer_views) {
    usize size = (buffer_view.byte_length + 3) & ~3;
    blob_size += size;
  }
  for (const GltfImage &image : images) {
    if (image.buffer_view == -1) {
      continue;
    }
    const GltfBufferView &view = gltf->buffer_views[image.buffer_view];
    blob_size += view.byte_length;
  }
  Span<std::byte> blob = Span<std::byte>::allocate(arena, blob_size);
  usize blob_offset = 0;

  for (usize accessor_index : range(accessors.size())) {
    GltfAccessor accessor = accessors[accessor_index];
    GltfBufferView *buffer_view = &buffer_views[accessor_index];
    buffer_view->byte_offset = blob_offset;
    GltfAccessor src_accessor = src_accessors[accessor_index];
    GltfBufferView src_buffer_view =
        gltf->buffer_views[src_accessor.buffer_view];
    Span<const std::byte> src_blob =
        gltf->buffers[src_buffer_view.buffer].bytes.subspan(
            src_accessor.byte_offset + src_buffer_view.byte_offset);

    ren_assert(accessor.count == src_accessor.count);
    usize count = accessor.count;

    usize src_packed_stride = gltf_accessor_packed_stride(
        src_accessor.type, src_accessor.component_type);
    usize src_stride = src_buffer_view.byte_stride ? src_buffer_view.byte_stride
                                                   : src_packed_stride;
    usize dst_stride =
        gltf_accessor_packed_stride(accessor.type, accessor.component_type);
    usize dst_size = count * dst_stride;

    if (src_accessor.component_type == accessor.component_type and
        src_accessor.type == accessor.type) {
      if (src_stride == dst_stride) {
        usize src_size = count * src_packed_stride;
        ren_assert(blob_offset + src_size <= blob.size());
        copy(src_blob.subspan(0, src_size), &blob[blob_offset]);
      } else {
        for (usize i : range(count)) {
          for (usize j : range(dst_stride)) {
            blob[blob_offset + i * dst_stride + j] =
                src_blob[i * src_stride + j];
          }
        }
      }
    } else {
      ScratchArena scratch;
      if (src_stride != src_packed_stride) {
        Span<std::byte> packed_blob =
            Span<std::byte>::allocate(scratch, src_packed_stride * count);
        for (usize i : range(count)) {
          for (usize j : range(src_packed_stride)) {
            packed_blob[i * src_packed_stride + j] =
                src_blob[i * src_stride + j];
          }
        }
        src_blob = packed_blob;
      }

      if (accessor.component_type == GLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        if (src_accessor.component_type == GLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
          copy(Span((const u8 *)src_blob.data(), count),
               (u32 *)&blob[blob_offset]);
        } else {
          ren_assert(src_accessor.component_type ==
                     GLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
          copy(Span((const u16 *)src_blob.data(), count),
               (u32 *)&blob[blob_offset]);
        }
      } else {
        ren_assert(accessor.component_type == GLTF_COMPONENT_TYPE_FLOAT);
        ren_assert(src_accessor.component_type == GLTF_COMPONENT_TYPE_FLOAT or
                   src_accessor.normalized);
        if (src_accessor.type == GLTF_ACCESSOR_TYPE_VEC2) {
          ren_assert(accessor.type == GLTF_ACCESSOR_TYPE_VEC2);
          Span dst((glm::vec2 *)&blob[blob_offset], count);
          if (src_accessor.component_type ==
              GLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            Span src((const glm::vec<2, u8> *)src_blob.data(), count);
            for (usize i : range(count)) {
              dst[i] = glm::unpackUnorm<float>(src[i]);
            }
          } else {
            ren_assert(src_accessor.component_type ==
                       GLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
            Span src((const glm::vec<2, u16> *)src_blob.data(), count);
            for (usize i : range(count)) {
              dst[i] = glm::unpackUnorm<float>(src[i]);
            }
          }
        } else {
          ren_assert(accessor.type == GLTF_ACCESSOR_TYPE_VEC4);
          Span dst((glm::vec4 *)&blob[blob_offset], count);
          if (src_accessor.type == GLTF_ACCESSOR_TYPE_VEC3) {
            if (src_accessor.component_type ==
                GLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
              Span src((const glm::vec<3, u8> *)src_blob.data(), count);
              for (usize i : range(count)) {
                dst[i] = {glm::unpackUnorm<float>(src[i]), 1.0f};
              }
            } else if (src_accessor.component_type ==
                       GLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
              Span src((const glm::vec<3, u16> *)src_blob.data(), count);
              for (usize i : range(count)) {
                dst[i] = {glm::unpackUnorm<float>(src[i]), 1.0f};
              }
            } else {
              ren_assert(src_accessor.component_type ==
                         GLTF_COMPONENT_TYPE_FLOAT);
              Span src((const glm::vec3 *)src_blob.data(), count);
              for (usize i : range(count)) {
                dst[i] = {src[i], 1.0f};
              }
            }
          } else {
            ren_assert(src_accessor.type == GLTF_ACCESSOR_TYPE_VEC4);
            if (src_accessor.component_type ==
                GLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
              Span src((const glm::vec<4, u8> *)src_blob.data(), count);
              for (usize i : range(count)) {
                dst[i] = glm::unpackUnorm<float>(src[i]);
              }
            } else {
              ren_assert(src_accessor.component_type ==
                         GLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
              Span src((const glm::vec<4, u16> *)src_blob.data(), count);
              for (usize i : range(count)) {
                dst[i] = glm::unpackUnorm<float>(src[i]);
              }
            }
          }
        }
      }
    }
    blob_offset = (blob_offset + dst_size + 3) & ~3;
  }

  for (const GltfImage &image : images) {
    if (image.buffer_view == -1) {
      continue;
    }
    const GltfBufferView &view = gltf->buffer_views[image.buffer_view];
    Span<const std::byte> src = gltf->buffers[view.buffer].bytes.subspan(
        view.byte_offset, view.byte_length);
    copy(src, &blob[blob_offset]);
    blob_offset += src.size();
  }

  ren_assert(blob_offset == blob.size());

  if (flags.is_set(GltfOptimize::NormalizeSceneBounds)) {
    usize primitive_count = 0;
    Span<usize> primitive_offsets =
        Span<usize>::allocate(scratch, meshes.size());
    for (usize mesh_index : range(meshes.size())) {
      primitive_offsets[mesh_index] = primitive_count;
      primitive_count += meshes[mesh_index].primitives.size();
    }

    struct BB {
      glm::vec3 min = glm::vec3(FLT_MAX);
      glm::vec3 max = glm::vec3(FLT_MIN);
    };
    Span<BB> primitive_bbs = Span<BB>::allocate(scratch, primitive_count);
    for (usize mesh_index : range(meshes.size())) {
      const GltfMesh &mesh = meshes[mesh_index];
      for (usize primitive_index : range(mesh.primitives.size())) {
        i32 position_accessor_index =
            gltf_find_attribute_by_semantic(mesh.primitives[primitive_index],
                                            GltfAttributeSemantic::POSITION)
                ->accessor;
        GltfAccessor positions = accessors[position_accessor_index];
        GltfBufferView buffer_view = buffer_views[positions.buffer_view];
        ren_assert(positions.byte_offset == 0);
        ren_assert(positions.type == GLTF_ACCESSOR_TYPE_VEC3);
        ren_assert(positions.component_type == GLTF_COMPONENT_TYPE_FLOAT);
        BB bb;
        for (glm::vec3 position :
             Span((const glm::vec3 *)&blob[buffer_view.byte_offset],
                  positions.count)) {
          bb.min = glm::min(bb.min, position);
          bb.max = glm::max(bb.max, position);
        }
        primitive_bbs[primitive_offsets[mesh_index] + primitive_index] = bb;
      }
    }

    Span<i32> node_parents = Span<i32>::allocate(scratch, nodes.size());
    fill(node_parents, -1);
    for (usize node_index : range(nodes.size())) {
      const GltfNode &node = nodes[node_index];
      for (i32 child_node_index : node.children) {
        node_parents[child_node_index] = node_index;
      }
    }

    Span<glm::mat4> node_global_transforms =
        Span<glm::mat4>::allocate(scratch, nodes.size());

    DynamicArray<i32> stack;
    for (usize node_index : range(nodes.size())) {
      if (node_parents[node_index] == -1) {
        node_global_transforms[node_index] = nodes[node_index].matrix;
        stack.push(scratch, nodes[node_index].children);
      }
    }
    while (not stack.is_empty()) {
      i32 node_index = stack.pop();
      i32 parent_index = node_parents[node_index];
      const GltfNode &node = nodes[node_index];
      stack.push(scratch, node.children);
      node_global_transforms[node_index] =
          node_global_transforms[parent_index] * node.matrix;
    }

    Span<GltfNode> new_nodes =
        Span<GltfNode>::allocate(scratch, nodes.size() + scenes.size());
    copy(nodes, new_nodes.data());
    i32 root_node_index = nodes.size();
    for (GltfScene &scene : scenes) {
      BB bb;
      stack.push(scratch, scene.nodes);
      while (not stack.is_empty()) {
        i32 node_index = stack.pop();
        const GltfNode &node = nodes[node_index];
        stack.push(scratch, node.children);
        if (node.mesh == -1) {
          continue;
        }
        const GltfMesh &mesh = meshes[node.mesh];
        for (usize primitive_index : range(mesh.primitives.size())) {
          BB primitive_bb =
              primitive_bbs[primitive_offsets[node.mesh] + primitive_index];
          glm::mat4 transform = node_global_transforms[node_index];
          glm::vec4 transformed_min =
              transform * glm::vec4(primitive_bb.min, 1.0f);
          transformed_min /= transformed_min.w;
          glm::vec4 transformed_max =
              transform * glm::vec4(primitive_bb.max, 1.0f);
          transformed_max /= transformed_max.w;
          glm::vec3 bb_min = glm::min(transformed_min, transformed_max);
          glm::vec3 bb_max = glm::max(transformed_min, transformed_max);
          bb.min = glm::min(bb.min, bb_min);
          bb.max = glm::max(bb.max, bb_max);
        }
      }
      glm::vec3 max_abs = glm::max(glm::abs(bb.min), glm::abs(bb.max));
      float scale =
          1.0f / (glm::max(max_abs.x, glm::max(max_abs.y, max_abs.z)));
      GltfNode root = {
          .name = "Root",
          .children = scene.nodes,
          .matrix = glm::scale(glm::vec3(scale)),
      };
      new_nodes[root_node_index] = root;
      scene.nodes = Span({root_node_index}).copy(scratch);
    }
    nodes = new_nodes;
  }

  if (flags.is_set(GltfOptimize::CollapseSceneHierarchy)) {
    Span<i32> node_parents = Span<i32>::allocate(scratch, nodes.size());
    fill(node_parents, -1);
    for (usize node_index : range(nodes.size())) {
      const GltfNode &node = nodes[node_index];
      for (i32 child : node.children) {
        node_parents[child] = node_index;
      }
    }

    Span<glm::mat4> node_global_transforms =
        Span<glm::mat4>::allocate(scratch, nodes.size());

    DynamicArray<i32> stack;
    for (usize node_index : range(nodes.size())) {
      if (node_parents[node_index] == -1) {
        node_global_transforms[node_index] = nodes[node_index].matrix;
        stack.push(scratch, nodes[node_index].children);
      }
    }
    while (not stack.is_empty()) {
      i32 node_index = stack.pop();
      i32 parent_index = node_parents[node_index];
      const GltfNode &node = nodes[node_index];
      stack.push(scratch, node.children);
      node_global_transforms[node_index] =
          node_global_transforms[parent_index] * node.matrix;
    }

    Span<i32> node_remap = Span<i32>::allocate(scratch, nodes.size());
    DynamicArray<GltfNode> remapped_nodes;
    remapped_nodes.reserve(scratch, nodes.size());
    for (usize node_index : range(nodes.size())) {
      GltfNode node = nodes[node_index];
      if (node.children.is_empty()) {
        node_remap[node_index] = remapped_nodes.size();
        node.name = {};
        node.matrix = node_global_transforms[node_index];
        remapped_nodes.push(node);
      } else {
        node_remap[node_index] = -1;
      }
    }
    for (GltfScene &scene : scenes) {
      DynamicArray<i32> scene_nodes;
      stack.clear();
      stack.push(scratch, scene.nodes);
      while (not stack.is_empty()) {
        i32 node_index = stack.pop();
        const GltfNode &node = nodes[node_index];
        if (not node.children.is_empty()) {
          stack.push(scratch, node.children);
          continue;
        }
        ren_assert(node_remap[node_index] != -1);
        scene_nodes.push(scratch, node_remap[node_index]);
      }
      scene.nodes = scene_nodes;
    }
    nodes = remapped_nodes;
  }

  gltf->scenes = scenes.copy(arena);
  for (GltfScene &scene : gltf->scenes) {
    scene.nodes = scene.nodes.copy(arena);
  }

  gltf->nodes = nodes.copy(arena);
  for (GltfNode &node : gltf->nodes) {
    node.children = node.children.copy(arena);
  }

  gltf->meshes = meshes.copy(arena);
  if (flags.is_set(GltfOptimize::RemoveImages)) {
    gltf->images = {};
  }

  gltf->materials = materials.copy(arena);
  gltf->textures = textures.copy(arena);
  gltf->samplers = samplers.copy(arena);
  gltf->images = images.copy(arena);

  gltf->accessors = Span(accessors).copy(arena);
  gltf->buffer_views = Span(buffer_views).copy(arena);
  gltf->buffers = Span({GltfBuffer{
                           .byte_length = blob.size(),
                           .bytes = blob,
                       }})
                      .copy(arena);
}

} // namespace ren
