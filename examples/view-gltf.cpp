#include "ImGuiApp.hpp"
#include "core/IO.hpp"
#include "ren/baking/image.hpp"
#include "ren/baking/mesh.hpp"

#include <cstdint>
#include <cxxopts.hpp>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <glm/glm.hpp>
#include <glm/gtc/color_space.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tiny_gltf.h>

namespace chrono = std::chrono;
namespace fs = std::filesystem;

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

auto duration_as_float(chrono::nanoseconds time) -> float {
  return chrono::duration_cast<chrono::duration<float>>(time).count();
}

auto load_gltf(const fs::path &path) -> Result<tinygltf::Model> {
  tinygltf::TinyGLTF loader;
  tinygltf::Model model;
  std::string err;
  std::string warn;

  if (not fs::exists(path)) {
    bail("Failed to open file {}: doesn't exist", path);
  }

  log("Load scene...");
  auto start = chrono::steady_clock::now();

  bool ret = true;
  if (path.extension() == ".gltf") {
    ret = loader.LoadASCIIFromFile(&model, &err, &warn, path.string());
  } else if (path.extension() == ".glb") {
    ret = loader.LoadBinaryFromFile(&model, &err, &warn, path.string());
  } else {
    bail("Failed to load glTF file {}: invalid extension {}", path,
         path.extension());
  }

  auto end = chrono::steady_clock::now();

  log("Loaded scene in {:.3f}s", duration_as_float(end - start));

  if (!ret) {
    while (not err.empty() and err.back() == '\n') {
      err.pop_back();
    }
    bail("{}", std::move(err));
  }

  if (not warn.empty()) {
    fmt::println(stderr, "{}", warn);
  }

  return std::move(model);
}

auto get_image_format(unsigned components, int pixel_type, bool srgb)
    -> Result<TinyImageFormat> {
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
  bail("Unknown format: {}/{}, sRGB: {}", components, pixel_type, srgb);
}

auto get_sampler_wrap_mode(int mode) -> Result<ren::WrappingMode> {
  switch (mode) {
  default:
    bail("Unknown sampler wrapping mode {}", mode);
  case TINYGLTF_TEXTURE_WRAP_REPEAT:
    return ren::WrappingMode::Repeat;
  case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
    return ren::WrappingMode::ClampToEdge;
  case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
    return ren::WrappingMode::MirroredRepeat;
  }
}

auto get_sampler(const tinygltf::Sampler &sampler) -> Result<ren::SamplerDesc> {
  ren::Filter mag_filter, min_filter, mip_filter;
  switch (sampler.magFilter) {
  default:
    bail("Unknown sampler magnification filter {}", sampler.magFilter);
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
    bail("Unknown sampler magnification filter {}", sampler.magFilter);
  case TINYGLTF_TEXTURE_FILTER_LINEAR:
    bail("Linear minification filter not implemented");
  case TINYGLTF_TEXTURE_FILTER_NEAREST:
    bail("Nearest minification filter not implemented");
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
  OK(ren::WrappingMode wrap_u, get_sampler_wrap_mode(sampler.wrapS));
  OK(ren::WrappingMode wrap_v, get_sampler_wrap_mode(sampler.wrapT));
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

// boost::hash_combine
template <typename T>
auto hash_combine(ren::u64 hash, const T &value) -> ren::u64 {
  hash ^= std::hash<T>()(value) + 0x9e3779b9 + (hash << 6U) + (hash >> 2U);
  return hash;
}

template <> struct std::hash<GltfMeshDesc> {
  auto operator()(const GltfMeshDesc &desc) const -> size_t {
    size_t seed = 0;
    hash_combine(seed, desc.positions);
    hash_combine(seed, desc.normals);
    hash_combine(seed, desc.tangents);
    hash_combine(seed, desc.colors);
    hash_combine(seed, desc.uvs);
    hash_combine(seed, desc.indices);
    return seed;
  }
};

template <typename T>
auto deindex_attibute(std::span<const T> attribute,
                      std::span<const uint32_t> indices, std::span<T> out) {
  assert(out.size() == indices.size());
  for (size_t i = 0; i < indices.size(); ++i) {
    out[i] = attribute[indices[i]];
  }
}

class SceneWalker {
public:
  SceneWalker(tinygltf::Model model, ren::Scene *scene) {
    m_model = std::move(model);
    m_scene = scene;
  }

  auto walk(int scene) -> Result<void> {
    if (not m_model.extensionsRequired.empty()) {
      bail("Required glTF extensions not supported: {}",
           m_model.extensionsRequired);
    }

    if (not m_model.extensionsUsed.empty()) {
      warn("Ignoring used glTF extensions: {}", m_model.extensionsUsed);
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
      bail("Scene index {} out of bounds", scene);
    }

    TRY_TO(walk_scene(m_model.scenes[scene]));

    return {};
  }

private:
  template <typename T,
            std::invocable<T> F = decltype([](T data) { return data; })>
  auto get_accessor_data(const tinygltf::Accessor &accessor,
                         F transform = {}) const
      -> std::vector<std::invoke_result_t<F, T>> {
    const tinygltf::BufferView &view = m_model.bufferViews[accessor.bufferView];
    std::span<const unsigned char> src_data = m_model.buffers[view.buffer].data;
    src_data = src_data.subspan(view.byteOffset, view.byteLength);
    src_data = src_data.subspan(accessor.byteOffset);
    size_t stride = view.byteStride;
    if (stride == 0) {
      stride = sizeof(T);
    }
    std::vector<std::invoke_result_t<F, T>> data(accessor.count);
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

  auto create_mesh(const GltfMeshDesc &desc) -> Result<ren::MeshId> {
    const tinygltf::Accessor *positions = get_accessor(desc.positions);
    if (!positions) {
      bail("Primitive doesn't have POSITION attribute");
    }
    const tinygltf::Accessor *normals = get_accessor(desc.normals);
    if (!normals) {
      bail("Primitive doesn't have NORMAL attribute");
    }
    const tinygltf::Accessor *tangents = get_accessor(desc.tangents);
    const tinygltf::Accessor *colors = get_accessor(desc.colors);
    const tinygltf::Accessor *uvs = get_accessor(desc.uvs);
    const tinygltf::Accessor *indices = get_accessor(desc.indices);

    if (positions->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT or
        positions->type != TINYGLTF_TYPE_VEC3) {
      bail("Invalid primitive POSITION attribute format: {}/{}",
           positions->componentType, positions->type);
    }
    std::vector<glm::vec3> positions_data =
        get_accessor_data<glm::vec3>(*positions);

    if (normals->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT or
        normals->type != TINYGLTF_TYPE_VEC3) {
      bail("Invalid primitive NORMAL attribute format: {}/{}",
           normals->componentType, normals->type);
    }
    std::vector<glm::vec3> normals_data =
        get_accessor_data<glm::vec3>(*normals);

    std::vector<glm::vec4> tangents_data;
    if (tangents) {
      if (tangents->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT or
          tangents->type != TINYGLTF_TYPE_VEC4) {
        bail("Invalid primitive TANGENT attribute format: {}/{}",
             tangents->componentType, tangents->type);
      }
      tangents_data = get_accessor_data<glm::vec4>(*tangents);
    }

    OK(auto colors_data, [&]() -> Result<std::vector<glm::vec4>> {
      if (!colors) {
        return {};
      }
      if (colors->type == TINYGLTF_TYPE_VEC3) {
        if (colors->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
          return get_accessor_data<glm::vec3>(
              *colors, [](glm::vec3 color) { return glm::vec4(color, 1.0f); });
        }
        if (colors->normalized) {
          if (colors->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            return get_accessor_data<glm::u8vec3>(
                *colors, [](glm::u8vec3 color) {
                  return glm::vec4(glm::unpackUnorm<float>(color), 1.0f);
                });
          }
          if (colors->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            return get_accessor_data<glm::u16vec3>(
                *colors, [](glm::u16vec3 color) {
                  return glm::vec4(glm::unpackUnorm<float>(color), 1.0f);
                });
          }
        }
      }
      if (colors->type == TINYGLTF_TYPE_VEC4) {
        if (colors->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
          return get_accessor_data<glm::vec4>(*colors);
        }
        if (colors->normalized) {
          if (colors->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            return get_accessor_data<glm::u8vec4>(
                *colors, [](glm::u8vec4 color) {
                  return glm::unpackUnorm<float>(color);
                });
          }
          if (colors->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            return get_accessor_data<glm::u16vec4>(
                *colors, [](glm::u16vec4 color) {
                  return glm::unpackUnorm<float>(color);
                });
          }
        }
      }
      bail("Invalid primitive COLOR_0 attribute format: {}/{}, normalized: {}",
           colors->componentType, colors->type, colors->normalized);
    }());

    OK(auto tex_coords_data, [&]() -> Result<std::vector<glm::vec2>> {
      if (!uvs) {
        return {};
      }
      if (uvs->type == TINYGLTF_TYPE_VEC2) {
        if (uvs->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
          return get_accessor_data<glm::vec2>(*uvs);
        }
        if (uvs->normalized) {
          if (uvs->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            return get_accessor_data<glm::u8vec2>(*uvs, [](glm::u8vec2 color) {
              return glm::unpackUnorm<float>(color);
            });
          }
          if (uvs->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            return get_accessor_data<glm::u16vec2>(
                *uvs, [](glm::u16vec2 color) {
                  return glm::unpackUnorm<float>(color);
                });
          }
        }
      }
      bail("Invalid primitive TEXCOORD_0 attribute format: "
           "{}/{}, normalized: {}",
           uvs->componentType, uvs->type, uvs->normalized);
    }());

    OK(auto indices_data, [&]() -> Result<std::vector<uint32_t>> {
      if (not indices->normalized and indices->type == TINYGLTF_TYPE_SCALAR) {
        if (indices->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
          return get_accessor_data<uint8_t>(
              *indices, [](uint8_t index) -> uint32_t { return index; });
        }
        if (indices->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
          return get_accessor_data<uint16_t>(
              *indices, [](uint16_t index) -> uint32_t { return index; });
        }
        if (indices->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
          return get_accessor_data<uint32_t>(*indices);
        }
      }
      bail("Invalid primitive indices format: "
           "{}/{}, normalized: {}",
           indices->componentType, indices->type, indices->normalized);
    }());

    OK(auto blob, ren::bake_mesh_to_memory({
                      .num_vertices = positions_data.size(),
                      .positions = positions_data.data(),
                      .normals = normals_data.data(),
                      .tangents = tangents_data.data(),
                      .uvs = tex_coords_data.data(),
                      .colors = colors_data.data(),
                      .num_indices = indices_data.size(),
                      .indices = indices_data.data(),
                  }));
    auto [blob_data, blob_size] = blob;
    OK(ren::MeshId mesh, ren::create_mesh(m_scene, blob_data, blob_size));
    std::free(blob_data);

    return mesh;
  }

  auto get_or_create_mesh(const tinygltf::Primitive &primitive)
      -> Result<ren::MeshId> {
    auto get_attribute_accessor_index =
        [&](const std::string &attribute) -> int {
      auto it = primitive.attributes.find(attribute);
      if (it == primitive.attributes.end()) {
        return -1;
      }
      return it->second;
    };
    GltfMeshDesc desc = {
        .positions = get_attribute_accessor_index("POSITION"),
        .normals = get_attribute_accessor_index("NORMAL"),
        .tangents = get_attribute_accessor_index("TANGENT"),
        .colors = get_attribute_accessor_index("COLOR_0"),
        .uvs = get_attribute_accessor_index("TEXCOORD_0"),
        .indices = primitive.indices,
    };
    ren::MeshId &mesh = m_mesh_cache[desc];
    if (!mesh) {
      std::string buffer;
      auto warn_unused_attribute = [&](std::string_view attribute, int start) {
        for (int index = start;; ++index) {
          buffer.clear();
          fmt::format_to(std::back_inserter(buffer), "{}_{}", attribute, index);
          if (get_attribute_accessor_index(buffer) < 0) {
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
        bail("Unsupported primitive mode {}", primitive.mode);
      }
      if (not primitive.targets.empty()) {
        warn("Ignoring {} primitive morph targets", primitive.targets.size());
      }
      OK(mesh, create_mesh(desc));
    }
    return mesh;
  }

  auto get_image_info(int image, bool srgb = false)
      -> Result<ren::TextureInfo> {
    const tinygltf::Image &gltf_image = m_model.images[image];
    assert(gltf_image.image.size() == gltf_image.width * gltf_image.height *
                                          gltf_image.component *
                                          gltf_image.bits / 8);
    OK(TinyImageFormat format,
       get_image_format(gltf_image.component, gltf_image.pixel_type, srgb));
    return {{
        .format = format,
        .width = unsigned(gltf_image.width),
        .height = unsigned(gltf_image.height),
        .data = gltf_image.image.data(),
    }};
  }

  auto get_texture_sampler(int texture) const -> Result<ren::SamplerDesc> {
    int sampler = m_model.textures[texture].sampler;
    if (sampler < 0) {
      bail("Default sampler not implemented");
    }
    return get_sampler(m_model.samplers[sampler]);
  }

  auto create_material(int index) -> Result<ren::MaterialId> {
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
          bail("Unsupported base color texture coordinate set {}",
               base_color_texture.texCoord);
        }
        int src = m_model.textures[base_color_texture.index].source;
        ren::ImageId &image = m_color_image_cache[src];
        if (!image) {
          OK(ren::TextureInfo texture_info, get_image_info(src, true));
          OK(auto blob, ren::bake_normal_map_to_memory(texture_info));
          auto [blob_data, blob_size] = blob;
          OK(image, create_image(m_scene, blob_data, blob_size));
          std::free(blob_data);
        }
        desc.base_color_texture.image = image;
        OK(desc.base_color_texture.sampler,
           get_texture_sampler(base_color_texture.index));
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
          bail("Unsupported metallic-roughness texture coordinate set {}",
               metallic_roughness_texture.texCoord);
        }
        int roughness_metallic_src =
            m_model.textures[metallic_roughness_texture.index].source;
        int occlusion_src = -1;
        if (occlusion_texture.index >= 0) {
          if (occlusion_texture.texCoord > 0) {
            bail("Unsupported occlusion texture coordinate set {}",
                 occlusion_texture.texCoord);
          }
          occlusion_src = m_model.textures[occlusion_texture.index].source;
        }
        auto it = std::ranges::find_if(
            m_orm_image_cache, [&](std::tuple<int, int, ren::ImageId> t) {
              auto [rm, o, i] = t;
              return rm == roughness_metallic_src and o == occlusion_src;
            });
        if (it != m_orm_image_cache.end()) {
          desc.orm_texture.image = std::get<2>(*it);
        } else {
          OK(ren::TextureInfo roughness_metallic_info,
             get_image_info(roughness_metallic_src));
          ren::TextureInfo occlusion_info;
          if (occlusion_src >= 0) {
            OK(occlusion_info, get_image_info(occlusion_src));
          }
          OK(auto blob, ren::bake_orm_map_to_memory(roughness_metallic_info,
                                                    occlusion_info));
          auto [blob_data, blob_size] = blob;
          OK(desc.orm_texture.image,
             create_image(m_scene, blob_data, blob_size));
          std::free(blob_data);
          m_orm_image_cache.emplace_back(roughness_metallic_src, occlusion_src,
                                         desc.orm_texture.image);
        }
        OK(desc.orm_texture.sampler,
           get_texture_sampler(metallic_roughness_texture.index));
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
          bail("Unsupported normal texture coordinate set {}",
               normal_texture.texCoord);
        }
        int src = m_model.textures[normal_texture.index].source;
        ren::ImageId &image = m_normal_image_cache[src];
        if (!image) {
          OK(ren::TextureInfo texture_info, get_image_info(src));
          OK(auto blob, ren::bake_normal_map_to_memory(texture_info));
          auto [blob_data, blob_size] = blob;
          OK(image, create_image(m_scene, blob_data, blob_size));
          std::free(blob_data);
        }
        desc.normal_texture.image = image;
        OK(desc.normal_texture.sampler,
           get_texture_sampler(normal_texture.index));
        desc.normal_texture.scale = normal_texture.scale;
      }
    }

    if (material.emissiveTexture.index >= 0 or
        material.emissiveFactor != std::vector{0.0, 0.0, 0.0}) {
      bail("Emissive materials not implemented");
    }

    if (material.alphaMode != "OPAQUE") {
      bail("Translucent materials not implemented");
    }

    if (material.doubleSided) {
      bail("Double sided materials not implemented");
    }

    return ren::create_material(m_scene, desc)
        .transform_error(get_error_string);
  }

  auto get_or_create_material(int index) -> Result<ren::MaterialId> {
    assert(index >= 0);
    if (index >= m_material_cache.size()) {
      m_material_cache.resize(index + 1);
    }
    ren::MaterialId &material = m_material_cache[index];
    if (!material) {
      OK(material, create_material(index));
    }
    return material;
  }

  auto create_mesh_instance(const tinygltf::Primitive &primitive,
                            const glm::mat4 &transform)
      -> Result<ren::MeshInstanceId> {
    OK(ren::MaterialId material, get_or_create_material(primitive.material));
    OK(ren::MeshId mesh, get_or_create_mesh(primitive));
    OK(ren::MeshInstanceId mesh_instance,
       ren::create_mesh_instance(m_scene,
                                 {
                                     .mesh = mesh,
                                     .material = material,
                                 })
           .transform_error(get_error_string));
    set_mesh_instance_transform(m_scene, mesh_instance, transform);
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

  auto walk_node(const tinygltf::Node &node, const glm::mat4 &parent_transform)
      -> Result<void> {
    int node_index = &node - m_model.nodes.data();
    glm::mat4 transform = parent_transform * get_node_local_transform(node);

    if (node.mesh >= 0) {
      const tinygltf::Mesh &mesh = m_model.meshes[node.mesh];
      for (const tinygltf::Primitive &primitive : mesh.primitives) {
        auto res = create_mesh_instance(primitive, transform);
        if (not res.has_value()) {
          warn("Failed to create mesh instance for mesh {} "
               "primitive {} in node {}: {}",
               node.mesh, &primitive - &mesh.primitives[0], node_index,
               res.error());
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
      TRY_TO(walk_node(m_model.nodes[child], transform));
    }

    return {};
  };

  auto walk_scene(const tinygltf::Scene &scene) -> Result<void> {
    auto transform = glm::mat4_cast(
        glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    for (int node : scene.nodes) {
      TRY_TO(walk_node(m_model.nodes[node], transform));
    }
    return {};
  }

private:
  tinygltf::Model m_model;
  ren::Scene *m_scene = nullptr;
  std::unordered_map<GltfMeshDesc, ren::MeshId> m_mesh_cache;
  std::unordered_map<int, ren::ImageId> m_color_image_cache;
  std::vector<std::tuple<int, int, ren::ImageId>> m_orm_image_cache;
  std::unordered_map<int, ren::ImageId> m_normal_image_cache;
  std::vector<ren::MaterialId> m_material_cache;
};

struct ViewGltfOptions {
  fs::path path;
  unsigned scene = 0;
  fs::path env_map;
};

class ViewGlTFApp : public ImGuiApp {
public:
  auto init(const ViewGltfOptions &options) -> Result<void> {
    TRY_TO(ImGuiApp::init(fmt::format("View glTF: {}", options.path).c_str()));
    OK(tinygltf::Model model, load_gltf(options.path));
    SceneWalker scene_walker(std::move(model), get_scene());
    TRY_TO(scene_walker.walk(options.scene));
    ren::Scene *scene = get_scene();

    auto env_map = [&]() -> Result<ren::ImageId> {
      if (options.env_map.empty()) {
        return {};
      }

      FILE *f = ren::fopen(options.env_map, "rb");
      if (!f) {
        bail("Failed to open {}", options.env_map);
      }

      std::vector<std::byte> blob(fs::file_size(options.env_map));
      size_t num_read = std::fread(blob.data(), 1, blob.size(), f);
      std::fclose(f);
      if (num_read != blob.size()) {
        bail("Failed to read from {}", options.env_map);
      }

      return ren::create_image(scene, blob).transform_error(get_error_string);
    }();

    if (env_map and *env_map) {
      TRY_TO(ren::set_environment_map(scene, *env_map));
    } else {
      if (!env_map) {
        warn("Failed to load environment map: {}", env_map.error());
      }
      OK(auto directional_light,
         ren::create_directional_light(scene, {
                                                  .color = {1.0f, 1.0f, 1.0f},
                                                  .illuminance = 100'000.0f,
                                                  .origin = {0.0f, 0.0f, 1.0f},
                                              }));
      ren::set_environment_color(
          scene,
          glm::convertSRGBToLinear(glm::vec3(78, 159, 229) / 255.0f) * 8000.0f);
    }
    return {};
  }

  [[nodiscard]] static auto run(const ViewGltfOptions &options) -> int {
    return AppBase::run<ViewGlTFApp>(options);
  }

protected:
  auto process_event(const SDL_Event &event) -> Result<void> override {
    TRY_TO(ImGuiApp::process_event(event));
    ImGuiIO &io = ImGui::GetIO();
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
    return {};
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

  auto process_frame(chrono::nanoseconds dt_ns) -> Result<void> override {
    if (ImGui::GetCurrentContext()) {
      draw_camera_imgui(m_camera_params);
    }

    ren::Scene *scene = get_scene();

    float dt = duration_as_float(dt_ns);

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
      ren::CameraId camera = get_camera();

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

    return {};
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

int main(int argc, const char *argv[]) {
  cxxopts::Options options("view-gltf", "A glTF 2.0 viewer for ren");
  // clang-format off
  options.add_options()
      ("file", "path to glTF file", cxxopts::value<fs::path>())
      ("scene", "index of scene to view", cxxopts::value<unsigned>()->default_value("0"))
      ("env-map", "path to environment map", cxxopts::value<fs::path>())
      ("h,help", "show this message")
  ;
  // clang-format on
  options.parse_positional({"file"});
  options.positional_help("file");
  cxxopts::ParseResult result = options.parse(argc, argv);
  if (result.count("help") or not result.count("file")) {
    fmt::println("{}", options.help());
    return 0;
  }

  return ViewGlTFApp::run({
      .path = result["file"].as<fs::path>(),
      .scene = result["scene"].as<unsigned>(),
      .env_map = result.count("env-map") ? result["env-map"].as<fs::path>()
                                         : fs::path(),
  });
}
