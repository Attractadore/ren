#pragma once
#include "Array.hpp"
#include "FileSystem.hpp"
#include "Optional.hpp"
#include "Result.hpp"
#include "Span.hpp"
#include "String.hpp"

#include <glm/ext/quaternion_float.hpp>
#include <glm/mat4x4.hpp>

namespace ren {

enum class GltfError {
  IO,
  JSON,
  Unsupported,
  InvalidFormat,
  ValidationFailed,
};

struct GltfErrorInfo {
  GltfError error;
  String8 message;
};

enum GltfComponentType {
  GLTF_COMPONENT_TYPE_BYTE = 5120,
  GLTF_COMPONENT_TYPE_UNSIGNED_BYTE = 5121,
  GLTF_COMPONENT_TYPE_SHORT = 5122,
  GLTF_COMPONENT_TYPE_UNSIGNED_SHORT = 5123,
  GLTF_COMPONENT_TYPE_UNSIGNED_INT = 5125,
  GLTF_COMPONENT_TYPE_FLOAT = 5126,
};

inline usize gltf_component_type_size(GltfComponentType type) {
  switch (type) {
  case GLTF_COMPONENT_TYPE_BYTE:
  case GLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    return 1;
  case GLTF_COMPONENT_TYPE_SHORT:
  case GLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    return 2;
  case GLTF_COMPONENT_TYPE_UNSIGNED_INT:
  case GLTF_COMPONENT_TYPE_FLOAT:
    return 4;
  }
  unreachable();
}

enum GltfAccessorType {
  GLTF_ACCESSOR_TYPE_SCALAR,
  GLTF_ACCESSOR_TYPE_VEC2,
  GLTF_ACCESSOR_TYPE_VEC3,
  GLTF_ACCESSOR_TYPE_VEC4,
  GLTF_ACCESSOR_TYPE_MAT2,
  GLTF_ACCESSOR_TYPE_MAT3,
  GLTF_ACCESSOR_TYPE_MAT4
};

inline usize gltf_accessor_type_element_count(GltfAccessorType type) {
  switch (type) {
  case GLTF_ACCESSOR_TYPE_SCALAR:
    return 1;
  case GLTF_ACCESSOR_TYPE_VEC2:
    return 2;
  case GLTF_ACCESSOR_TYPE_VEC3:
    return 3;
  case GLTF_ACCESSOR_TYPE_VEC4:
    return 4;
  case GLTF_ACCESSOR_TYPE_MAT2:
    return 4;
  case GLTF_ACCESSOR_TYPE_MAT3:
    return 9;
  case GLTF_ACCESSOR_TYPE_MAT4:
    return 16;
  }
  unreachable();
}

inline usize gltf_accessor_packed_stride(GltfAccessorType accessor_type,
                                         GltfComponentType component_type) {
  return gltf_component_type_size(component_type) *
         gltf_accessor_type_element_count(accessor_type);
}

enum GltfTopology {
  GLTF_TOPOLOGY_POINTS = 0,
  GLTF_TOPOLOGY_LINES = 1,
  GLTF_TOPOLOGY_LINE_LOOP = 2,
  GLTF_TOPOLOGY_LINE_STRIP = 3,
  GLTF_TOPOLOGY_TRIANGLES = 4,
  GLTF_TOPOLOGY_TRIANGLE_STRIP = 5,
  GLTF_TOPOLOGY_TRIANGLE_FAN = 6
};

String8 format_as(GltfTopology topology);

struct GltfVersion {
  u32 major = 0;
  u32 minor = 0;
};

struct GltfAsset {
  String8 generator;
  String8 copyright;
};

enum GltfTextureWrap {
  GLTF_TEXTURE_WRAP_REPEAT = 10497,
  GLTF_TEXTURE_WRAP_CLAMP_TO_EDGE = 33071,
  GLTF_TEXTURE_WRAP_MIRRORED_REPEAT = 33648,
};

enum GltfTextureFilter {
  GLTF_TEXTURE_FILTER_NEAREST = 9728,
  GLTF_TEXTURE_FILTER_LINEAR = 9729,
  GLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST = 9984,
  GLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST = 9985,
  GLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR = 9986,
  GLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR = 9987,
};

String8 format_as(GltfTextureFilter filter);

struct GltfSampler {
  String8 name;
  GltfTextureFilter mag_filter = GLTF_TEXTURE_FILTER_LINEAR;
  GltfTextureFilter min_filter = GLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR;
  GltfTextureWrap wrap_s = GLTF_TEXTURE_WRAP_REPEAT;
  GltfTextureWrap wrap_t = GLTF_TEXTURE_WRAP_REPEAT;
};

struct GltfTexture {
  i32 sampler = -1;
  i32 source = -1;
};

struct GltfImage {
  String8 name;
  i32 buffer_view = -1;
  String8 mime_type;
  String8 uri;
  Span<glm::u8vec4> pixels;
  u32 width = 0;
  u32 height = 0;
};

struct GltfTextureInfo {
  i32 index = -1;
  i32 tex_coord = 0;
};

struct GltfNormalTextureInfo {
  i32 index = -1;
  i32 tex_coord = 0;
  float scale = 1.0f;
};

struct GltfOcclusionTextureInfo {
  i32 index = -1;
  i32 tex_coord = 0;
  float strength = 1.0f;
};

struct GltfPbrMetallicRoughness {
  glm::vec4 base_color_factor = {1.0f, 1.0f, 1.0f, 1.0f};
  GltfTextureInfo base_color_texture;
  float metallic_factor = 1.0f;
  float roughness_factor = 1.0f;
  GltfTextureInfo metallic_roughness_texture;
};

enum GltfAlphaMode {
  GLTF_ALPHA_MODE_OPAQUE,
  GLTF_ALPHA_MODE_MASK,
  GLTF_ALPHA_MODE_BLEND,
};

struct GltfMaterial {
  String8 name;
  GltfPbrMetallicRoughness pbr_metallic_roughness;
  GltfNormalTextureInfo normal_texture;
  GltfOcclusionTextureInfo occlusion_texture;
  GltfTextureInfo emissive_texture;
  glm::vec3 emissive_factor = {0.0f, 0.0f, 0.0f};
  GltfAlphaMode alphaMode = GLTF_ALPHA_MODE_OPAQUE;
  float alphaCutoff = 0.5f;
  bool doubleSided = false;
};

struct GltfAccessor {
  String8 name;
  i32 buffer_view = -1;
  u32 byte_offset = 0;
  GltfComponentType component_type = GLTF_COMPONENT_TYPE_BYTE;
  bool normalized = false;
  u32 count = 0;
  GltfAccessorType type = GLTF_ACCESSOR_TYPE_SCALAR;
};

struct GltfBufferView {
  String8 name;
  i32 buffer = -1;
  u32 byte_offset = 0;
  u32 byte_length = 0;
  u32 byte_stride = 0;
};

struct GltfBuffer {
  String8 name;
  String8 uri;
  usize byte_length = 0;
  Span<std::byte> bytes;
};

enum class GltfAttributeSemantic {
  POSITION,
  NORMAL,
  TANGENT,
  TEXCOORD,
  COLOR,
  JOINTS,
  WEIGHTS,
  USER,
};

String8 format_as(GltfAttributeSemantic semantic);

struct GltfAttribute {
  String8 name;
  GltfAttributeSemantic semantic = {};
  i32 set_index = 0;
  i32 accessor = -1;

public:
  bool operator==(const GltfAttribute &) const = default;
};

struct GltfPrimitive {
  Span<GltfAttribute> attributes;
  i32 indices = -1;
  i32 material = -1;
  GltfTopology mode = GLTF_TOPOLOGY_TRIANGLES;
};

inline bool operator==(const GltfPrimitive &lhs, const GltfPrimitive &rhs) {
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

Optional<GltfAttribute> gltf_find_attribute_by_semantic(
    GltfPrimitive primitive, GltfAttributeSemantic semantic, i32 set_index = 0);

struct GltfMesh {
  String8 name;
  Span<GltfPrimitive> primitives;
};

struct GltfNode {
  String8 name;
  i32 camera = -1;
  i32 mesh = -1;
  i32 skin = -1;

  Span<i32> children;

  glm::mat4 matrix = glm::identity<glm::mat4>();

  glm::vec3 translation = {0.0f, 0.0f, 0.0f};
  glm::quat rotation = glm::identity<glm::quat>();
  glm::vec3 scale = {1.0f, 1.0f, 1.0f};
};

struct GltfScene {
  String8 name;
  Span<i32> nodes;
};

struct GltfMaterial;
struct GltfTexture;
struct GltfSkin;
struct GltfAnimation;
struct GltfCamera;

struct Gltf {
  GltfAsset asset;
  i32 scene = -1;
  Span<GltfScene> scenes;
  Span<GltfNode> nodes;
  Span<GltfMesh> meshes;
  Span<GltfMaterial> materials;
  Span<GltfTexture> textures;
  Span<GltfImage> images;
  Span<GltfSampler> samplers;
  Span<GltfAccessor> accessors;
  Span<GltfBufferView> buffer_views;
  Span<GltfBuffer> buffers;
  Span<GltfSkin> skins;
  Span<GltfAnimation> animations;
  Span<GltfCamera> cameras;
};

// clang-format off
REN_BEGIN_FLAGS_ENUM(GltfOptimize){
    REN_FLAG(RemoveCameras),
    REN_FLAG(RemoveMaterials),
    REN_FLAG(RemoveImages),
    REN_FLAG(RemoveSkins),
    REN_FLAG(RemoveAnimations),
    /// Convert all nodes into root nodes.
    REN_FLAG(CollapseSceneHierarchy),
    /// Remove unreferenced nodes or empty nodes without any children or contents.
    REN_FLAG(RemoveRedundantNodes),
    REN_FLAG(RemoveEmptyScenes),
    /// Uniformly scale scenes to be within [-1; 1].
    REN_FLAG(NormalizeSceneBounds),
    /// Remove unreferenced meshes and merge identical meshes.
    REN_FLAG(RemoveRedundantMeshes),
    /// Converts all mesh accessors into a full precision format:
    /// POSITION -> vec3
    /// NORMAL -> vec3
    /// TANGENTS -> vec4
    /// TEXCOORD -> vec2
    /// COLOR -> vec4
    /// Indices -> u32
    REN_FLAG(ConvertMeshAccessors),
} REN_END_FLAGS_ENUM(GltfOptimize);
// clang-format on

} // namespace ren

REN_ENABLE_FLAGS(ren::GltfOptimize);

namespace ren {

using GltfOptimizeFlags = Flags<GltfOptimize>;

struct GltfLoadedImage {
  Span<glm::u8vec4> pixels;
  u32 width = 0;
  u32 height = 0;
};

struct GltfLoadImageErrorInfo {
  String8 message;
};

using GltfLoadImageCallback = Result<GltfLoadedImage, GltfLoadImageErrorInfo> (
        *)(NotNull<Arena *> arena, void *context, Span<const std::byte> buffer);

#ifdef STBI_INCLUDE_STB_IMAGE_H
#ifndef REN_GLTF_STBI_CALLBACK_DEFINED
#define REN_GLTF_STBI_CALLBACK_DEFINED

inline Result<GltfLoadedImage, GltfLoadImageErrorInfo>
gltf_stbi_callback(NotNull<Arena *> arena, void *context,
                   Span<const std::byte> buffer) {
  int x, y, c;
  stbi_uc *stbi_pixels = stbi_load_from_memory((const stbi_uc *)buffer.data(),
                                               buffer.size(), &x, &y, &c, 4);
  if (!stbi_pixels) {
    return GltfLoadImageErrorInfo{
        .message = String8::init(arena, stbi_failure_reason()),
    };
  }
  Span<glm::u8vec4> pixels = Span<glm::u8vec4>::allocate(arena, x * y);
  copy((const glm::u8vec4 *)stbi_pixels, x * y, pixels.data());
  stbi_image_free(stbi_pixels);
  return GltfLoadedImage{
      .pixels = pixels,
      .width = (u32)x,
      .height = (u32)y,
  };
}

#endif
#endif

struct GltfLoadInfo {
  Path path;
  bool load_buffers = false;
  bool load_images = false;
  GltfLoadImageCallback load_image_callback = nullptr;
  void *load_image_context = nullptr;
  GltfOptimizeFlags optimize_flags = EmptyFlags;
};

[[nodiscard]] Result<Gltf, GltfErrorInfo>
load_gltf(NotNull<Arena *> arena, const GltfLoadInfo &load_info);

[[nodiscard]] Result<void, GltfErrorInfo>
gltf_load_buffers(NotNull<Arena *> arena, NotNull<Gltf *> gltf, Path gltf_path);

[[nodiscard]] Result<void, GltfErrorInfo>
gltf_load_images(NotNull<Arena *> arena, NotNull<Gltf *> gltf, Path gltf_path,
                 GltfLoadImageCallback cb, void *context);

void gltf_optimize(NotNull<Arena *> arena, NotNull<Gltf *> gltf,
                   GltfOptimizeFlags flags);

String8 gltf_serialize(NotNull<Arena *> arena, const Gltf &gltf);

} // namespace ren
