#pragma once
#include "Array.hpp"
#include "String.hpp"
#include "Span.hpp"
#include "Result.hpp"
#include "FileSystem.hpp"

namespace ren {
enum GltfError { 
  GLTF_OK,
  GLTF_ERROR_INVALID_SOURCE,
  GLTF_ERROR_NOT_SUPPORTED,
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

struct GltfAccessor {
  String8 name;
  i32 buffer_view;
  i32 buffer_offset;
  GltfComponentType component_type;
  bool normalized;
  i32 count;
  GltfType type;
  DynamicArray<float> min;
  DynamicArray<float> max;
  // NOTE: Sparse is not needed for now.
  // TODO: Extras
};

struct GltfBufferView {
  String8 name;
  i32 buffer;
  i32 byte_offset;
  i32 byte_length;
  i32 byte_stride;
  GltfBufferTarget target;
  // TODO: Extras
};

struct GltfBuffer {
  String8 name;
  String8 uri;
  i32 byte_length;
  DynamicArray<u8> data;
  // TODO: Extras
};

struct GltfAttribute {
  String8 name;
  i32 accessor;
};

struct GltfPrimitive {
  DynamicArray<GltfAttribute> attributes;
  i32 indices;
  i32 material;
  GltfTopology mode;
  // NOTE: According to the specification, there should also be a field Array<Array<GltfAttribute>> targets here.
  // TODO: Extras
};

struct GltfMesh {
  String8 name;
  DynamicArray<GltfPrimitive> primitives;
  DynamicArray<float> weights;
  // TODO: Extras
};

struct GltfNode {
  String8 name;
  i32 camera;
  i32 mesh;
  i32 skin;
  DynamicArray<i32> children;

  bool has_matrix;
  float matrix[16];

  float translation[3];
  float rotation[4];
  float scale[3];

  // NOTE: According to the specification, there should also be an Array<float> weights field here.
  // TODO: Extras
};

struct GltfScene {
  String8 name;
  DynamicArray<i32> nodes;
  // TODO: Extras
};

struct Gltf {
  GltfAsset asset;
  i32 scene;

  DynamicArray<GltfScene> scenes;
  DynamicArray<GltfNode> nodes;
  DynamicArray<GltfMesh> meshes;
  // TODO: materials
  // TODO: textures
  // TODO: images
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

Result<Gltf, GltfError> gltf_parse_file(NotNull<Arena *> arena, Path path);

constexpr String8 GLTF_ACCESSOR_TYPE_SCALAR = "SCALAR";
constexpr String8 GLTF_ACCESSOR_TYPE_VEC2 = "VEC2";
constexpr String8 GLTF_ACCESSOR_TYPE_VEC3 = "VEC3";
constexpr String8 GLTF_ACCESSOR_TYPE_VEC4 = "VEC4";
constexpr String8 GLTF_ACCESSOR_TYPE_MAT2 = "MAT2";
constexpr String8 GLTF_ACCESSOR_TYPE_MAT3 = "MAT3";
constexpr String8 GLTF_ACCESSOR_TYPE_MAT4 = "MAT4";

} // namespace ren
