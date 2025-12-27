#pragma once
#include "Array.hpp"
#include "FileSystem.hpp"
#include "Result.hpp"
#include "Span.hpp"
#include "String.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ren {
enum class GltfError {
  IO_Error,
  JSON_ParseError,
  JSON_UnexpectedValue,
  JSON_EpectedFieldNotFound,
  VALUE_OutOfRange,
  VALUE_UnknownType
};

struct GltfErrorInfo {
  GltfError error;
  String8 desc;
};

enum GltfComponentType {
  GLTF_COMPONENT_TYPE_BYTE = 5120,
  GLTF_COMPONENT_TYPE_UNSIGNED_BYTE = 5121,
  GLTF_COMPONENT_TYPE_SHORT = 5122,
  GLTF_COMPONENT_TYPE_UNSIGNED_SHORT = 5123,
  GLTF_COMPONENT_TYPE_UNSIGNED_INT = 5125,
  GLTF_COMPONENT_TYPE_FLOAT = 5126,
};

enum GltfType {
  GLTF_TYPE_SCALAR,
  GLTF_TYPE_VEC2,
  GLTF_TYPE_VEC3,
  GLTF_TYPE_VEC4,
  GLTF_TYPE_MAT2,
  GLTF_TYPE_MAT3,
  GLTF_TYPE_MAT4
};

enum GltfTopology {
  GLTF_TOPOLOGY_POINTS = 0,
  GLTF_TOPOLOGY_LINES = 1,
  GLTF_TOPOLOGY_LINE_LOOP = 2,
  GLTF_TOPOLOGY_LINE_STRIP = 3,
  GLTF_TOPOLOGY_TRIANGLES = 4,
  GLTF_TOPOLOGY_TRIANGLE_STRIP = 5,
  GLTF_TOPOLOGY_TRIANGLE_FAN = 6
};

enum GltfBufferTarget {
  GLTF_TARGET_NONE = 0,
  GLTF_TARGET_ARRAY_BUFFER = 34962,
  GLTF_TARGET_ELEMENT_ARRAY_BUFFER = 34963
};

struct GltfAsset {
  String8 version;
  String8 generator;
  String8 copyright;
  String8 min_version;
  // TODO: Extras
};

struct GltfImage {
  String8 name;
  i32 buffer_view = -1;
  String8 mime_type;
  String8 uri;
};

struct GltfAccessor {
  String8 name;
  i32 buffer_view = -1;
  u32 buffer_offset = 0;
  GltfComponentType component_type = GLTF_COMPONENT_TYPE_BYTE;
  bool normalized = false;
  u32 count = 0;
  GltfType type = GLTF_TYPE_SCALAR;
  StackArray<float, 16> min = {};
  StackArray<float, 16> max = {};
  // NOTE: Sparse is not needed for now.
  // TODO: Extras
};

struct GltfBufferView {
  String8 name;
  i32 buffer = -1;
  u32 byte_offset = 0;
  u32 byte_length = 1;
  u32 byte_stride = 4;
  GltfBufferTarget target;
  // TODO: Extras
};

struct GltfBuffer {
  String8 name;
  String8 uri;
  Span<u8> data;
  // TODO: Extras
};

struct GltfAttribute {
  String8 name;
  i32 accessor = -1;
};

inline bool operator==(const GltfAttribute& lhs, const GltfAttribute& rhs) {
  return lhs.accessor == rhs.accessor && lhs.name == rhs.name;
}

struct GltfPrimitive {
  DynamicArray<GltfAttribute> attributes;
  i32 indices = -1;
  i32 material = -1;
  GltfTopology mode;
  // NOTE: According to the specification, there should also be a field
  // Array<Array<GltfAttribute>> targets here.
  // TODO: Extras
};

inline bool operator==(const GltfPrimitive& lhs, const GltfPrimitive& rhs) {
  if (lhs.indices != rhs.indices) {
    return false;
  }
  if (lhs.attributes.m_size != rhs.attributes.m_size) {
    return false;
  }
  if (lhs.mode != rhs.mode) {
    return false;
  }

  for (usize i = 0; i < rhs.attributes.m_size; ++i) {
    if (lhs.attributes[i] != rhs.attributes[i]) {
      return false;
    }
  }
  return true;
}

inline bool operator!=(const GltfPrimitive& lhs, const GltfPrimitive& rhs) {
  return !(lhs == rhs);
}

struct GltfMesh {
  String8 name;
  DynamicArray<GltfPrimitive> primitives;
  // TODO: Extras
};

struct GltfNode {
  String8 name;
  i32 camera = -1;
  i32 mesh = -1;
  i32 skin = -1;
  DynamicArray<i32> children;

  glm::mat4 matrix = glm::identity<glm::mat4>();

  glm::vec3 translation = {0.0f, 0.0f, 0.0f};
  glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 scale = {1.0f, 1.0f, 1.0f};

  // NOTE: According to the specification, there should also be an Array<float>
  // weights field here.
  // TODO: Extras
};

struct GltfScene {
  String8 name;
  DynamicArray<i32> nodes;
  // TODO: Extras
};

struct Gltf {
  GltfAsset asset;
  i32 scene = -1;

  DynamicArray<GltfScene> scenes;
  DynamicArray<GltfNode> nodes;
  DynamicArray<GltfMesh> meshes;
  // TODO: materials
  // TODO: textures
  DynamicArray<GltfImage> images;
  // TODO: samplers
  DynamicArray<GltfAccessor> accessors;
  DynamicArray<GltfBufferView> buffer_views;
  DynamicArray<GltfBuffer> buffers;
  // TODO: skins
  // TODO: animations
  // TODO: cameras

  // TODO: extensions_used
  // TODO: extensions_required
  // TODO: Extras
};

Result<Gltf, GltfErrorInfo> load_gltf(NotNull<Arena *> arena, Path path);
String8 to_string(NotNull<Arena *> arena, const Gltf &gltf);

constexpr String8 GLTF_ACCESSOR_TYPE_SCALAR = "SCALAR";
constexpr String8 GLTF_ACCESSOR_TYPE_VEC2 = "VEC2";
constexpr String8 GLTF_ACCESSOR_TYPE_VEC3 = "VEC3";
constexpr String8 GLTF_ACCESSOR_TYPE_VEC4 = "VEC4";
constexpr String8 GLTF_ACCESSOR_TYPE_MAT2 = "MAT2";
constexpr String8 GLTF_ACCESSOR_TYPE_MAT3 = "MAT3";
constexpr String8 GLTF_ACCESSOR_TYPE_MAT4 = "MAT4";

} // namespace ren
