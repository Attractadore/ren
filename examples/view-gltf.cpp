#include "ImGuiApp.hpp"
#include "ren/baking/image.hpp"
#include "ren/baking/mesh.hpp"
#include "ren/core/Algorithm.hpp"
#include "ren/core/Chrono.hpp"
#include "ren/core/CmdLine.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/Span.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/color_space.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tiny_gltf.h>

enum Projection {
  PROJECTION_PERSPECTIVE,
  PROJECTION_ORTHOGRAPHIC,
};

struct CameraParams {
  Projection projection = PROJECTION_PERSPECTIVE;
  float hfov = 90.0f;
  float orthographic_width = 1.0f;
};

void draw_camera_imgui(CameraParams &params) {
  if (ImGui::CollapsingHeader("Camera")) {
    ImGui::SeparatorText("Projection");
    int projection = params.projection;
    ImGui::RadioButton("Perspective", &projection, PROJECTION_PERSPECTIVE);
    if (projection == PROJECTION_PERSPECTIVE) {
      ImGui::SliderFloat("Field of view", &params.hfov, 5.0f, 175.0f,
                         "%.0f deg");
    }
    ImGui::RadioButton("Orthographic", &projection, PROJECTION_ORTHOGRAPHIC);
    if (projection == PROJECTION_ORTHOGRAPHIC) {
      ImGui::SliderFloat("Box width", &params.orthographic_width, 0.1f, 10.0f,
                         "%.1f m");
    }
    params.projection = (Projection)projection;
  }
}

#define warn(msg, ...) fmt::println("Warn: " msg __VA_OPT__(, ) __VA_ARGS__)
#define log(msg, ...) fmt::println("Info: " msg __VA_OPT__(, ) __VA_ARGS__)

tinygltf::Model load_gltf(ren::Path path) {
  tinygltf::TinyGLTF loader;
  tinygltf::Model model;
  std::string err;
  std::string warn;

  if (not path.exists().value_or(false)) {
    fmt::println(stderr, "Failed to open file {}: doesn't exist", path);
    exit(EXIT_FAILURE);
  }

  log("Load scene...");
  auto start = ren::clock();

  bool ret = true;
  if (path.extension() == ".gltf") {
    ret = loader.LoadASCIIFromFile(&model, &err, &warn,
                                   {path.m_str.begin(), path.m_str.end()});
  } else if (path.extension() == ".glb") {
    ret = loader.LoadBinaryFromFile(&model, &err, &warn,
                                    {path.m_str.begin(), path.m_str.end()});
  } else {
    fmt::println(stderr, "Failed to load glTF file {}: invalid extension {}",
                 path, path.extension());
    exit(EXIT_FAILURE);
  }

  auto end = ren::clock();

  log("Loaded scene in {:.3f}s", (end - start) / 1e9);

  if (!ret) {
    while (not err.empty() and err.back() == '\n') {
      err.pop_back();
    }
    fmt::println(stderr, "{}", err);
    exit(EXIT_FAILURE);
  }

  if (not warn.empty()) {
    fmt::println(stderr, "{}", warn);
  }

  return model;
}

TinyImageFormat get_image_format(unsigned components, int pixel_type,
                                 bool srgb) {
  if (pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
    if (components == 1) {
      return srgb ? TinyImageFormat_R8_SRGB : TinyImageFormat_R8_UNORM;
    }
    if (components == 2) {
      return srgb ? TinyImageFormat_R8G8_SRGB : TinyImageFormat_R8G8_UNORM;
    }
    if (components == 3) {
      return srgb ? TinyImageFormat_R8G8B8_SRGB : TinyImageFormat_R8G8B8_UNORM;
    }
    if (components == 4) {
      return srgb ? TinyImageFormat_R8G8B8A8_SRGB
                  : TinyImageFormat_R8G8B8A8_UNORM;
    }
  }
  if (pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT and not srgb) {
    if (components == 1) {
      return TinyImageFormat_R16_UNORM;
    }
    if (components == 2) {
      return TinyImageFormat_R16G16_UNORM;
    }
    if (components == 3) {
      return TinyImageFormat_R16G16B16_UNORM;
    }
    if (components == 4) {
      return TinyImageFormat_R16G16B16A16_UNORM;
    }
  }
  fmt::println(stderr, "Unknown format: {}/{}, sRGB: {}", components,
               pixel_type, srgb);
  return TinyImageFormat_UNDEFINED;
}

ren::WrappingMode get_sampler_wrap_mode(int mode) {
  switch (mode) {
  default:
    fmt::println(stderr, "Unknown sampler wrapping mode {}", mode);
    return ren::WrappingMode::ClampToEdge;
  case TINYGLTF_TEXTURE_WRAP_REPEAT:
    return ren::WrappingMode::Repeat;
  case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
    return ren::WrappingMode::ClampToEdge;
  case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
    return ren::WrappingMode::MirroredRepeat;
  }
}

ren::SamplerDesc get_sampler(const tinygltf::Sampler &sampler) {
  ren::Filter mag_filter, min_filter, mip_filter;
  switch (sampler.magFilter) {
  default:
    fmt::println(stderr, "Unknown sampler magnification filter {}",
                 sampler.magFilter);
    mag_filter = ren::Filter::Linear;
    break;
  case -1:
  case TINYGLTF_TEXTURE_FILTER_LINEAR:
    mag_filter = ren::Filter::Linear;
    break;
  case TINYGLTF_TEXTURE_FILTER_NEAREST:
    mag_filter = ren::Filter::Nearest;
    break;
  }
  switch (sampler.minFilter) {
  default:
    fmt::println(stderr, "Unknown sampler magnification filter {}",
                 sampler.magFilter);
    min_filter = ren::Filter::Linear;
    mip_filter = ren::Filter::Linear;
    break;
  case TINYGLTF_TEXTURE_FILTER_LINEAR:
    fmt::println(stderr, "Linear minification filter not implemented");
    min_filter = ren::Filter::Linear;
    mip_filter = ren::Filter::Linear;
    break;
  case TINYGLTF_TEXTURE_FILTER_NEAREST:
    fmt::println(stderr, "Nearest minification filter not implemented");
    min_filter = ren::Filter::Nearest;
    mip_filter = ren::Filter::Nearest;
    break;
  case -1:
  case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
    min_filter = ren::Filter::Linear;
    mip_filter = ren::Filter::Linear;
    break;
  case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
    min_filter = ren::Filter::Linear;
    mip_filter = ren::Filter::Nearest;
    break;
  case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
    min_filter = ren::Filter::Nearest;
    mip_filter = ren::Filter::Linear;
    break;
  case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
    min_filter = ren::Filter::Nearest;
    mip_filter = ren::Filter::Nearest;
    break;
  }
  ren::WrappingMode wrap_u = get_sampler_wrap_mode(sampler.wrapS);
  ren::WrappingMode wrap_v = get_sampler_wrap_mode(sampler.wrapT);
  return ren::SamplerDesc{
      .mag_filter = mag_filter,
      .min_filter = min_filter,
      .mipmap_filter = mip_filter,
      .wrap_u = wrap_u,
      .wrap_v = wrap_v,
  };
};

struct GltfMeshDesc {
  int positions = -1;
  int normals = -1;
  int tangents = -1;
  int colors = -1;
  int uvs = -1;
  int indices = -1;

  auto operator<=>(const GltfMeshDesc &) const = default;
};

struct MeshCacheItem {
  GltfMeshDesc desc;
  ren::Handle<ren::Mesh> handle;
};

struct ImageCacheItem {
  int id = -1;
  ren::Handle<ren::Image> handle;
};

struct OrmImageCacheItem {
  int rm_id = -1;
  int o_id = -1;
  ren::Handle<ren::Image> handle;
};

template <typename T>
auto deindex_attibute(ren::Span<const T> attribute,
                      ren::Span<const uint32_t> indices, ren::Span<T> out) {
  ren_assert(out.m_size == indices.m_size);
  for (size_t i = 0; i < indices.m_size; ++i) {
    out[i] = attribute[indices[i]];
  }
}

class SceneWalker {
public:
  SceneWalker(tinygltf::Model model, ren::NotNull<ren::Arena *> load_arena,
              ren::NotNull<ren::Arena *> frame_arena,
              ren::NotNull<ren::Scene *> scene) {
    m_model = std::move(model);
    m_load_arena = load_arena;
    m_frame_arena = frame_arena;
    m_scene = scene;
  }

  void walk(ren::u32 scene) {
    if (not m_model.extensionsRequired.empty()) {
      fmt::println(stderr, "Required glTF extensions not supported");
      exit(EXIT_FAILURE);
    }

    if (not m_model.extensionsUsed.empty()) {
      warn("Ignoring used glTF extensions");
    }

    if (not m_model.animations.empty()) {
      warn("Ignoring {} animations", m_model.animations.size());
    }

    if (not m_model.skins.empty()) {
      warn("Ignoring {} skins", m_model.skins.size());
    }

    if (not m_model.cameras.empty()) {
      warn("Ignoring {} cameras", m_model.cameras.size());
    }

    if (scene >= m_model.scenes.size()) {
      fmt::println(stderr, "Scene index {} out of bounds", scene);
      exit(EXIT_FAILURE);
    }

    walk_scene(m_model.scenes[scene]);
  }

private:
  template <typename T,
            std::invocable<T> F = decltype([](T data) { return data; })>
  ren::Span<std::invoke_result_t<F, T>>
  get_accessor_data(ren::NotNull<ren::Arena *> arena,
                    const tinygltf::Accessor &accessor,
                    F transform = {}) const {
    const tinygltf::BufferView &view = m_model.bufferViews[accessor.bufferView];
    ren::Span<const unsigned char> src_data(
        m_model.buffers[view.buffer].data.data(),
        m_model.buffers[view.buffer].data.size());
    src_data = src_data.subspan(view.byteOffset, view.byteLength);
    src_data = src_data.subspan(accessor.byteOffset);
    size_t stride = view.byteStride;
    if (stride == 0) {
      stride = sizeof(T);
    }
    auto data =
        ren::Span<std::invoke_result_t<F, T>>::allocate(arena, accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
      data[i] = transform(*(T *)&src_data[i * stride]);
    }
    return data;
  }

  auto get_accessor(int index) const -> const tinygltf::Accessor * {
    if (index < 0) {
      return nullptr;
    }
    assert(index < m_model.accessors.size());
    return &m_model.accessors[index];
  }

  ren::Handle<ren::Mesh> create_mesh(const GltfMeshDesc &desc) {
    ren::ScratchArena scratch(m_load_arena);
    const tinygltf::Accessor *positions = get_accessor(desc.positions);
    if (!positions) {
      fmt::println(stderr, "Primitive doesn't have POSITION attribute");
      return ren::NullHandle;
    }
    const tinygltf::Accessor *normals = get_accessor(desc.normals);
    if (!normals) {
      fmt::println(stderr, "Primitive doesn't have NORMAL attribute");
      return ren::NullHandle;
    }
    const tinygltf::Accessor *tangents = get_accessor(desc.tangents);
    const tinygltf::Accessor *colors = get_accessor(desc.colors);
    const tinygltf::Accessor *uvs = get_accessor(desc.uvs);
    const tinygltf::Accessor *indices = get_accessor(desc.indices);

    if (positions->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT or
        positions->type != TINYGLTF_TYPE_VEC3) {
      fmt::println(stderr, "Invalid primitive POSITION attribute format: {}/{}",
                   positions->componentType, positions->type);
      return ren::NullHandle;
    }
    ren::Span<glm::vec3> positions_data =
        get_accessor_data<glm::vec3>(scratch, *positions);

    if (normals->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT or
        normals->type != TINYGLTF_TYPE_VEC3) {
      fmt::println(stderr, "Invalid primitive NORMAL attribute format: {}/{}",
                   normals->componentType, normals->type);
      return ren::NullHandle;
    }
    ren::Span<glm::vec3> normals_data =
        get_accessor_data<glm::vec3>(scratch, *normals);

    ren::Span<glm::vec4> tangents_data;
    if (tangents) {
      if (tangents->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT or
          tangents->type != TINYGLTF_TYPE_VEC4) {
        fmt::println(stderr,
                     "Invalid primitive TANGENT attribute format: {}/{}",
                     tangents->componentType, tangents->type);
        return ren::NullHandle;
      }
      tangents_data = get_accessor_data<glm::vec4>(scratch, *tangents);
    }

    auto colors_data = [&]() -> ren::Span<glm::vec4> {
      if (!colors) {
        return {};
      }
      if (colors->type == TINYGLTF_TYPE_VEC3) {
        if (colors->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
          return get_accessor_data<glm::vec3>(
              scratch, *colors,
              [](glm::vec3 color) { return glm::vec4(color, 1.0f); });
        }
        if (colors->normalized) {
          if (colors->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            return get_accessor_data<glm::u8vec3>(
                scratch, *colors, [](glm::u8vec3 color) {
                  return glm::vec4(glm::unpackUnorm<float>(color), 1.0f);
                });
          }
          if (colors->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            return get_accessor_data<glm::u16vec3>(
                scratch, *colors, [](glm::u16vec3 color) {
                  return glm::vec4(glm::unpackUnorm<float>(color), 1.0f);
                });
          }
        }
      }
      if (colors->type == TINYGLTF_TYPE_VEC4) {
        if (colors->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
          return get_accessor_data<glm::vec4>(scratch, *colors);
        }
        if (colors->normalized) {
          if (colors->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            return get_accessor_data<glm::u8vec4>(
                scratch, *colors, [](glm::u8vec4 color) {
                  return glm::unpackUnorm<float>(color);
                });
          }
          if (colors->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            return get_accessor_data<glm::u16vec4>(
                scratch, *colors, [](glm::u16vec4 color) {
                  return glm::unpackUnorm<float>(color);
                });
          }
        }
      }
      fmt::println(
          stderr,
          "Invalid primitive COLOR_0 attribute format: {}/{}, normalized: {}",
          colors->componentType, colors->type, colors->normalized);
      return {};
    }();

    auto tex_coords_data = [&]() -> ren::Span<glm::vec2> {
      if (!uvs) {
        return {};
      }
      if (uvs->type == TINYGLTF_TYPE_VEC2) {
        if (uvs->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
          return get_accessor_data<glm::vec2>(scratch, *uvs);
        }
        if (uvs->normalized) {
          if (uvs->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            return get_accessor_data<glm::u8vec2>(
                scratch, *uvs, [](glm::u8vec2 color) {
                  return glm::unpackUnorm<float>(color);
                });
          }
          if (uvs->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            return get_accessor_data<glm::u16vec2>(
                scratch, *uvs, [](glm::u16vec2 color) {
                  return glm::unpackUnorm<float>(color);
                });
          }
        }
      }
      fmt::println(stderr,
                   "Invalid primitive TEXCOORD_0 attribute format: "
                   "{}/{}, normalized: {}",
                   uvs->componentType, uvs->type, uvs->normalized);
      return {};
    }();

    auto indices_data = [&]() -> ren::Span<uint32_t> {
      if (not indices->normalized and indices->type == TINYGLTF_TYPE_SCALAR) {
        if (indices->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
          return get_accessor_data<uint8_t>(
              scratch, *indices,
              [](uint8_t index) -> uint32_t { return index; });
        }
        if (indices->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
          return get_accessor_data<uint16_t>(
              scratch, *indices,
              [](uint16_t index) -> uint32_t { return index; });
        }
        if (indices->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
          return get_accessor_data<uint32_t>(scratch, *indices);
        }
      }
      fmt::println(stderr,
                   "Invalid primitive indices format: "
                   "{}/{}, normalized: {}",
                   indices->componentType, indices->type, indices->normalized);
      return {};
    }();

    ren::Blob blob = ren::bake_mesh_to_memory(
        scratch, {
                     .num_vertices = positions_data.m_size,
                     .positions = positions_data.m_data,
                     .normals = normals_data.m_data,
                     .tangents = tangents_data.m_data,
                     .uvs = tex_coords_data.m_data,
                     .colors = colors_data.m_data,
                     .indices = indices_data,
                 });
    return ren::create_mesh(m_frame_arena, m_scene, blob.data, blob.size);
  }

  ren::Handle<ren::Mesh>
  get_or_create_mesh(const tinygltf::Primitive &primitive) {
    auto get_attribute_accessor_index = [&](ren::String8 attribute) -> int {
      for (const auto &[key, value] : primitive.attributes) {
        if (ren::String8(key.data(), key.size()) == attribute) {
          return value;
        }
      }
      return -1;
    };
    GltfMeshDesc desc = {
        .positions = get_attribute_accessor_index("POSITION"),
        .normals = get_attribute_accessor_index("NORMAL"),
        .tangents = get_attribute_accessor_index("TANGENT"),
        .colors = get_attribute_accessor_index("COLOR_0"),
        .uvs = get_attribute_accessor_index("TEXCOORD_0"),
        .indices = primitive.indices,
    };
    const MeshCacheItem *cached =
        ren::find_if(ren::Span(m_mesh_cache), [&](const MeshCacheItem &item) {
          return item.desc == desc;
        });
    if (cached) {
      return cached->handle;
    }

    auto warn_unused_attribute = [&](ren::String8 attribute, int start) {
      for (int index = start;; ++index) {
        ren::ScratchArena scratch(m_load_arena);
        auto buffer = ren::StringBuilder::init(scratch);
        format_to(&buffer, "{}_{}", attribute, index);
        if (get_attribute_accessor_index(buffer.string()) < 0) {
          break;
        }
        warn("Ignoring primitive attribute {}", buffer);
      }
    };
    warn_unused_attribute("TEXCOORD", 1);
    warn_unused_attribute("COLOR", 1);
    warn_unused_attribute("JOINTS", 0);
    warn_unused_attribute("WEIGHTS", 0);
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
      fmt::println(stderr, "Unsupported primitive mode {}", primitive.mode);
      return ren::NullHandle;
    }
    if (not primitive.targets.empty()) {
      warn("Ignoring {} primitive morph targets", primitive.targets.size());
    }
    ren::Handle<ren::Mesh> mesh = create_mesh(desc);
    m_mesh_cache.push(m_load_arena, {desc, mesh});
    return mesh;
  }

  ren::TextureInfo get_image_info(int image, bool srgb = false) {
    const tinygltf::Image &gltf_image = m_model.images[image];
    assert(gltf_image.image.size() == gltf_image.width * gltf_image.height *
                                          gltf_image.component *
                                          gltf_image.bits / 8);
    TinyImageFormat format =
        get_image_format(gltf_image.component, gltf_image.pixel_type, srgb);
    return {
        .format = format,
        .width = unsigned(gltf_image.width),
        .height = unsigned(gltf_image.height),
        .data = gltf_image.image.data(),
    };
  }

  ren::SamplerDesc get_texture_sampler(int texture) const {
    int sampler = m_model.textures[texture].sampler;
    if (sampler < 0) {
      fmt::println(stderr, "Default sampler not implemented");
      return {};
    }
    return get_sampler(m_model.samplers[sampler]);
  }

  ren::Handle<ren::Material> create_material(int index) {
    const tinygltf::Material &material = m_model.materials[index];
    ren::MaterialCreateInfo desc = {};

    assert(material.pbrMetallicRoughness.baseColorFactor.size() == 4);
    desc.base_color_factor =
        glm::make_vec4(material.pbrMetallicRoughness.baseColorFactor.data());

    {
      const tinygltf::TextureInfo &base_color_texture =
          material.pbrMetallicRoughness.baseColorTexture;
      if (base_color_texture.index >= 0) {
        if (base_color_texture.texCoord > 0) {
          fmt::println(stderr,
                       "Unsupported base color texture coordinate set {}",
                       base_color_texture.texCoord);
          return ren::NullHandle;
        }
        int src = m_model.textures[base_color_texture.index].source;
        const ImageCacheItem *cached = ren::find_if(
            ren::Span(m_color_image_cache),
            [&](const ImageCacheItem &item) { return item.id == src; });
        if (cached) {
          desc.base_color_texture.image = cached->handle;
        } else {
          ren::ScratchArena scratch(m_load_arena);
          ren::TextureInfo texture_info = get_image_info(src, true);
          auto blob = ren::bake_normal_map_to_memory(scratch, texture_info);
          ren::Handle<ren::Image> image =
              create_image(m_frame_arena, m_scene, blob.data, blob.size);
          m_color_image_cache.push(m_load_arena, {src, image});
          desc.base_color_texture.image = image;
        }
        desc.base_color_texture.sampler =
            get_texture_sampler(base_color_texture.index);
      }
    }

    desc.metallic_factor = material.pbrMetallicRoughness.metallicFactor;
    desc.roughness_factor = material.pbrMetallicRoughness.roughnessFactor;

    {
      const tinygltf::TextureInfo &metallic_roughness_texture =
          material.pbrMetallicRoughness.metallicRoughnessTexture;
      const tinygltf::OcclusionTextureInfo &occlusion_texture =
          material.occlusionTexture;
      if (metallic_roughness_texture.index >= 0) {
        if (metallic_roughness_texture.texCoord > 0) {
          fmt::println(
              stderr,
              "Unsupported metallic-roughness texture coordinate set {}",
              metallic_roughness_texture.texCoord);
          return ren::NullHandle;
        }
        int roughness_metallic_src =
            m_model.textures[metallic_roughness_texture.index].source;
        int occlusion_src = -1;
        if (occlusion_texture.index >= 0) {
          if (occlusion_texture.texCoord > 0) {
            fmt::println(stderr,
                         "Unsupported occlusion texture coordinate set {}",
                         occlusion_texture.texCoord);
            return ren::NullHandle;
          }
          occlusion_src = m_model.textures[occlusion_texture.index].source;
        }
        const OrmImageCacheItem *cached = ren::find_if(
            ren::Span(m_orm_image_cache), [&](const OrmImageCacheItem &item) {
              return item.rm_id == roughness_metallic_src and
                     item.o_id == occlusion_src;
            });
        if (cached) {
          desc.orm_texture.image = cached->handle;
        } else {
          ren::ScratchArena scratch(m_load_arena);
          ren::TextureInfo roughness_metallic_info =
              get_image_info(roughness_metallic_src);
          ren::TextureInfo occlusion_info;
          if (occlusion_src >= 0) {
            occlusion_info = get_image_info(occlusion_src);
          }
          auto blob = ren::bake_orm_map_to_memory(
              scratch, roughness_metallic_info, occlusion_info);
          ren::Handle<ren::Image> image =
              create_image(m_frame_arena, m_scene, blob.data, blob.size);
          m_orm_image_cache.push(m_load_arena,
                                 {
                                     .rm_id = roughness_metallic_src,
                                     .o_id = occlusion_src,
                                     .handle = image,
                                 });
          desc.orm_texture.image = image;
        }
        desc.orm_texture.sampler =
            get_texture_sampler(metallic_roughness_texture.index);
      } else if (occlusion_texture.index >= 0) {
        warn("Occlusion textures without a metallic-roughness texture are not "
             "supported");
      }
    }

    {
      const tinygltf::NormalTextureInfo &normal_texture =
          material.normalTexture;
      if (normal_texture.index >= 0) {
        if (normal_texture.texCoord > 0) {
          fmt::println(stderr, "Unsupported normal texture coordinate set {}",
                       normal_texture.texCoord);
          return ren::NullHandle;
        }
        int src = m_model.textures[normal_texture.index].source;
        const ImageCacheItem *cached = ren::find_if(
            ren::Span(m_normal_image_cache),
            [&](const ImageCacheItem &item) { return item.id == src; });
        if (cached) {
          desc.normal_texture.image = cached->handle;
        } else {
          ren::ScratchArena scratch(m_load_arena);
          ren::TextureInfo texture_info = get_image_info(src);
          auto blob = ren::bake_normal_map_to_memory(scratch, texture_info);
          ren::Handle<ren::Image> image =
              create_image(m_frame_arena, m_scene, blob.data, blob.size);
          m_normal_image_cache.push(m_load_arena, {src, image});
          desc.normal_texture.image = image;
        }

        desc.normal_texture.sampler = get_texture_sampler(normal_texture.index);
        desc.normal_texture.scale = normal_texture.scale;
      }
    }

    glm::vec3 emissive = glm::make_vec3(material.emissiveFactor.data());
    if (material.emissiveTexture.index >= 0 or
        emissive != glm::vec3{0.0f, 0.0f, 0.0f}) {
      fmt::println(stderr, "Emissive materials not implemented");
      return ren::NullHandle;
    }

    if (material.alphaMode != "OPAQUE") {
      fmt::println(stderr, "Translucent materials not implemented");
      return ren::NullHandle;
    }

    if (material.doubleSided) {
      fmt::println(stderr, "Double sided materials not implemented");
      return ren::NullHandle;
    }

    return ren::create_material(m_frame_arena, m_scene, desc);
  }

  ren::Handle<ren::Material> get_or_create_material(int index) {
    assert(index >= 0);
    while ((ren::u32)index >= m_material_cache.m_size) {
      m_material_cache.push(m_load_arena);
    }
    ren::Handle<ren::Material> &material = m_material_cache[index];
    if (!material) {
      material = create_material(index);
    }
    return material;
  }

  ren::Handle<ren::MeshInstance>
  create_mesh_instance(const tinygltf::Primitive &primitive,
                       const glm::mat4 &transform) {
    ren::Handle<ren::Material> material =
        get_or_create_material(primitive.material);
    ren::Handle<ren::Mesh> mesh = get_or_create_mesh(primitive);
    ren::Handle<ren::MeshInstance> mesh_instance =
        ren::create_mesh_instance(m_frame_arena, m_scene,
                                  {
                                      .mesh = mesh,
                                      .material = material,
                                  });
    ren::set_mesh_instance_transform(m_frame_arena, m_scene, mesh_instance,
                                     transform);
    return mesh_instance;
  }

  auto get_node_local_transform(const tinygltf::Node &node) -> glm::mat4 {
    if (not node.matrix.empty()) {
      assert(node.matrix.size() == 16);
      return glm::make_mat4(node.matrix.data());
    }
    glm::vec3 trans(0.0f);
    glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale(1.0f);
    if (not node.translation.empty()) {
      assert(node.translation.size() == 3);
      trans.x = node.translation[0];
      trans.y = node.translation[1];
      trans.z = node.translation[2];
    }
    if (not node.rotation.empty()) {
      assert(node.rotation.size() == 4);
      rot.x = node.rotation[0];
      rot.y = node.rotation[1];
      rot.z = node.rotation[2];
      rot.w = node.rotation[3];
    }
    if (not node.scale.empty()) {
      assert(node.scale.size() == 3);
      scale.x = node.scale[0];
      scale.y = node.scale[1];
      scale.z = node.scale[2];
    }
    auto local_transform = glm::identity<glm::mat4>();
    local_transform = glm::translate(local_transform, trans);
    local_transform = local_transform * glm::mat4_cast(rot);
    local_transform = glm::scale(local_transform, scale);
    return local_transform;
  }

  void walk_node(const tinygltf::Node &node,
                 const glm::mat4 &parent_transform) {
    int node_index = &node - m_model.nodes.data();
    glm::mat4 transform = parent_transform * get_node_local_transform(node);

    if (node.mesh >= 0) {
      const tinygltf::Mesh &mesh = m_model.meshes[node.mesh];
      for (const tinygltf::Primitive &primitive : mesh.primitives) {
        if (!create_mesh_instance(primitive, transform)) {
          fmt::println(stderr,
                       "Failed to create mesh instance for mesh {} "
                       "primitive {} in node {}",
                       node.mesh, &primitive - &mesh.primitives[0], node_index);
        }
      }
    }

    if (node.camera >= 0) {
      warn("Ignoring camera {} for node {}", node.camera, node_index);
    }

    if (node.skin >= 0) {
      warn("Ignoring skin {} for node {}", node.skin, node_index);
    }

    if (not node.weights.empty()) {
      warn("Ignoring weights for node {}", node_index);
    }

    for (int child : node.children) {
      walk_node(m_model.nodes[child], transform);
    }
  };

  void walk_scene(const tinygltf::Scene &scene) {
    auto transform = glm::mat4_cast(
        glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    for (int node : scene.nodes) {
      walk_node(m_model.nodes[node], transform);
    }
  }

private:
  tinygltf::Model m_model;
  ren::Arena *m_load_arena = nullptr;
  ren::Arena *m_frame_arena = nullptr;
  ren::Scene *m_scene = nullptr;
  ren::DynamicArray<MeshCacheItem> m_mesh_cache;
  ren::DynamicArray<ImageCacheItem> m_color_image_cache;
  ren::DynamicArray<OrmImageCacheItem> m_orm_image_cache;
  ren::DynamicArray<ImageCacheItem> m_normal_image_cache;
  ren::DynamicArray<ren::Handle<ren::Material>> m_material_cache;
};

struct ViewGltfOptions {
  ren::Path path;
  unsigned scene = 0;
  ren::Path env_map;
};

class ViewGlTFApp : public ImGuiApp {
public:
  void init(const ViewGltfOptions &options) {
    ren::ScratchArena scratch;
    ImGuiApp::init(format(scratch, "View glTF: {}", options.path));
    SceneWalker scene_walker(load_gltf(options.path), scratch, &m_frame_arena,
                             get_scene());
    scene_walker.walk(options.scene);
    ren::Scene *scene = get_scene();

    ren::Handle<ren::Image> env_map;
    if (options.env_map) {
      auto blob = ren::read<std::byte>(scratch, options.env_map);
      if (!blob) {
        fmt::println(stderr, "Failed to read {}: {}", options.env_map,
                     blob.error());
      } else {
        env_map = ren::create_image(&m_frame_arena, scene, *blob);
      }
    }

    if (env_map) {
      ren::set_environment_map(scene, env_map);
    } else {
      std::ignore =
          ren::create_directional_light(scene, {
                                                   .color = {1.0f, 1.0f, 1.0f},
                                                   .illuminance = 100'000.0f,
                                                   .origin = {0.0f, 0.0f, 1.0f},
                                               });
      ren::set_environment_color(
          scene,
          glm::convertSRGBToLinear(glm::vec3(78, 159, 229) / 255.0f) * 8000.0f);
    }
  }

  static void run(const ViewGltfOptions &options) {
    return AppBase::run<ViewGlTFApp>(options);
  }

protected:
  void process_event(const SDL_Event &event) override {
    ImGuiApp::process_event(event);
    switch (event.type) {
    default:
      break;
    case SDL_EVENT_MOUSE_WHEEL: {
      if (imgui_wants_capture_mouse()) {
        break;
      }
      m_distance =
          m_distance * glm::pow(2.0f, event.wheel.y / m_zoom_sensitivity);
    } break;
    }
  }

  struct InputState {
    float pitch = 0.0f;
    float yaw = 0.0f;
  };

  auto get_input_state() const -> InputState {
    InputState input;
    const bool *keys = SDL_GetKeyboardState(nullptr);
    if (keys[m_pitch_up_key]) {
      input.pitch += 1.0f;
    }
    if (keys[m_pitch_down_key]) {
      input.pitch -= 1.0f;
    }
    if (keys[m_yaw_left_key]) {
      input.yaw += 1.0f;
    }
    if (keys[m_yaw_right_key]) {
      input.yaw -= 1.0f;
    }
    return input;
  }

  void process_frame(ren::u64 dt_ns) override {
    if (ImGui::GetCurrentContext()) {
      draw_camera_imgui(m_camera_params);
    }

    ren::Scene *scene = get_scene();

    float dt = dt_ns / 1e9f;

    InputState input = get_input_state();

    m_yaw += m_yaw_speed * dt * input.yaw;
    m_pitch += m_pitch_speed * dt * input.pitch;
    m_pitch = glm::clamp(m_pitch, -glm::radians(80.0f), glm::radians(80.0f));

    glm::vec3 forward = {1.0f, 0.0f, 0.0f};
    glm::vec3 left = {0.0f, 1.0f, 0.0f};
    glm::vec3 up = {0.0f, 0.0f, 1.0f};

    glm::quat rot = glm::angleAxis(m_yaw, up);
    left = rot * left;
    rot = glm::angleAxis(m_pitch, left) * rot;
    forward = rot * forward;

    glm::vec3 position = -m_distance * forward;

    {
      ren::Handle<ren::Camera> camera = get_camera();

      ren::set_camera_transform(scene, camera,
                                {
                                    .position = position,
                                    .forward = forward,
                                    .up = up,
                                });

      switch (m_camera_params.projection) {
      case PROJECTION_PERSPECTIVE: {
        ren::set_camera_perspective_projection(
            scene, camera, {.hfov = glm::radians(m_camera_params.hfov)});
      } break;
      case PROJECTION_ORTHOGRAPHIC: {
        ren::set_camera_orthographic_projection(
            scene, camera, {.width = m_camera_params.orthographic_width});
      } break;
      }
    }
  }

private:
  SDL_Scancode m_pitch_up_key = SDL_SCANCODE_W;
  SDL_Scancode m_pitch_down_key = SDL_SCANCODE_S;
  SDL_Scancode m_yaw_left_key = SDL_SCANCODE_A;
  SDL_Scancode m_yaw_right_key = SDL_SCANCODE_D;

  float m_pitch_speed = glm::radians(45.0f);
  float m_pitch = glm::radians(45.0f);

  float m_yaw_speed = -glm::radians(45.0f);
  float m_yaw = 0.0f;

  float m_zoom_sensitivity = -25.0f;
  float m_distance = 3.0f;

  CameraParams m_camera_params;
};

enum ViewGltfCmdLineOptions {
  OPTION_FILE,
  OPTION_SCENE,
  OPTION_ENV_MAP,
  OPTION_HELP,
  OPTION_COUNT,
};

int main(int argc, const char *argv[]) {
  ren::ScratchArena::init_allocator();
  ren::Arena cmd_line = ren::Arena::init();

  // clang-format off
  ren::CmdLineOption options[] = {
      {OPTION_FILE, ren::CmdLinePath, "file", 0, "path to glTF file", ren::CmdLinePositional},
      {OPTION_SCENE, ren::CmdLineUInt, "scene", 0, "index of scene to view"},
      {OPTION_ENV_MAP, ren::CmdLinePath, "env-map", 0, "path to environment map"},
      {OPTION_HELP, ren::CmdLineFlag, "help", 'h', "show this message"},
  };
  // clang-format on
  ren::ParsedCmdLineOption parsed[OPTION_COUNT];
  bool success = parse_cmd_line(&cmd_line, argv, options, parsed);
  if (!success or parsed[OPTION_HELP].is_set) {
    ren::ScratchArena scratch;
    fmt::print("{}", cmd_line_help(scratch, argv[0], options));
    return EXIT_FAILURE;
  }

  ren::Path path = parsed[OPTION_FILE].as_path;
  ren::u32 scene = 0;
  if (parsed[OPTION_SCENE].is_set) {
    scene = parsed[OPTION_SCENE].as_uint;
  }
  ren::Path env_map;
  if (parsed[OPTION_ENV_MAP].is_set) {
    env_map = parsed[OPTION_ENV_MAP].as_path;
  }

  ViewGlTFApp::run({
      .path = path,
      .scene = scene,
      .env_map = env_map,
  });
}
