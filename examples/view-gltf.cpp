#include "ImGuiApp.hpp"

#include <boost/functional/hash.hpp>
#include <cstdint>
#include <cxxopts.hpp>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/ranges.h>
#include <glm/glm.hpp>
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
  ren::ExposureMode exposure = ren::ExposureMode::Camera;
  float camera_exposure_comensation = 0.0f;
  float iso = 100.0f;
  float aperture = 16.0f;
  float inv_shutter_time = 100.0f;
  float automatic_exposure_compensation = 0.0f;
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

    ImGui::SeparatorText("Exposure");
    int exposure = (int)params.exposure;
    ImGui::RadioButton("Physical camera", &exposure,
                       (int)ren::ExposureMode::Camera);
    if (params.exposure == ren::ExposureMode::Camera) {
      ImGui::SliderFloat("ISO", &params.iso, 100.0f, 3200.0f, "%.0f",
                         ImGuiSliderFlags_AlwaysClamp |
                             ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("Aperture", &params.aperture, 1.0f, 22.0f, "f/%.1f",
                         ImGuiSliderFlags_AlwaysClamp |
                             ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat(
          "Shutter time", &params.inv_shutter_time, 1.0f, 2000.0f, "%.0f 1/s",
          ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
      ImGui::InputFloat("Exposure compensation",
                        &params.camera_exposure_comensation, 1.0f, 10.0f,
                        "%.1f EV");
    }
    ImGui::RadioButton("Automatic", &exposure,
                       (int)ren::ExposureMode::Automatic);
    if (params.exposure == ren::ExposureMode::Automatic) {
      ImGui::InputFloat("Exposure compensation",
                        &params.automatic_exposure_compensation, 1.0f, 10.0f,
                        "%.1f EV");
    }
    params.exposure = (ren::ExposureMode)exposure;
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
    bail("Failed to open file {}: doesn't exist", path.c_str());
  }

  log("Load scene...");
  auto start = chrono::steady_clock::now();

  bool ret = true;
  if (path.extension() == ".gltf") {
    ret = loader.LoadASCIIFromFile(&model, &err, &warn, path.string());
  } else if (path.extension() == ".glb") {
    ret = loader.LoadBinaryFromFile(&model, &err, &warn, path.string());
  } else {
    bail("Failed to load glTF file {}: invalid extension {}", path.c_str(),
         path.extension().c_str());
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

template <> struct std::hash<GltfMeshDesc> {
  auto operator()(const GltfMeshDesc &desc) const -> size_t {
    size_t seed = 0;
    boost::hash_combine(seed, desc.positions);
    boost::hash_combine(seed, desc.normals);
    boost::hash_combine(seed, desc.tangents);
    boost::hash_combine(seed, desc.colors);
    boost::hash_combine(seed, desc.uvs);
    boost::hash_combine(seed, desc.indices);
    return seed;
  }
};

struct GltfImageDesc {
  int index : 31 = -1;
  bool srgb : 1 = false;

  auto operator<=>(const GltfImageDesc &) const = default;
};

template <> struct std::hash<GltfImageDesc> {
  auto operator()(const GltfImageDesc &desc) const -> size_t {
    size_t seed = 0;
    boost::hash_combine(seed, desc.index);
    boost::hash_combine(seed, desc.srgb);
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
  SceneWalker(tinygltf::Model model, ren::IScene &scene) {
    m_model = std::move(model);
    m_scene = &scene;
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

    return m_scene
        ->create_mesh({
            .positions = positions_data,
            .normals = normals_data,
            .tangents = tangents_data,
            .colors = colors_data,
            .uvs = tex_coords_data,
            .indices = indices_data,
        })
        .transform_error(get_error_string);
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

  auto create_image(const GltfImageDesc &desc) -> Result<ren::ImageId> {
    const tinygltf::Image &image = m_model.images[desc.index];
    assert(image.image.size() ==
           image.width * image.height * image.component * image.bits / 8);
    OK(TinyImageFormat format,
       get_image_format(image.component, image.pixel_type, desc.srgb));

    return m_scene
        ->create_image({
            .width = unsigned(image.width),
            .height = unsigned(image.height),
            .format = format,
            .data = image.image.data(),
        })
        .transform_error(get_error_string);
  }

  auto get_or_create_image(int index, bool srgb) -> Result<ren::ImageId> {
    GltfImageDesc desc = {
        .index = index,
        .srgb = srgb,
    };
    ren::ImageId &image = m_image_cache[desc];
    if (!image) {
      OK(image, create_image(desc));
    }
    return image;
  }

  auto get_or_create_texture_image(int index, bool srgb)
      -> Result<ren::ImageId> {
    int image = m_model.textures[index].source;
    return get_or_create_image(image, srgb);
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
        OK(desc.base_color_texture.image,
           get_or_create_texture_image(base_color_texture.index, true));
        OK(desc.base_color_texture.sampler,
           get_texture_sampler(base_color_texture.index));
      }
    }

    desc.metallic_factor = material.pbrMetallicRoughness.metallicFactor;
    desc.roughness_factor = material.pbrMetallicRoughness.roughnessFactor;

    {
      const tinygltf::TextureInfo &metallic_roughness_texture =
          material.pbrMetallicRoughness.metallicRoughnessTexture;
      if (metallic_roughness_texture.index >= 0) {
        if (metallic_roughness_texture.texCoord > 0) {
          bail("Unsupported metallic-roughness texture coordinate set {}",
               metallic_roughness_texture.texCoord);
        }
        OK(desc.metallic_roughness_texture.image,
           get_or_create_texture_image(metallic_roughness_texture.index,
                                       false));
        OK(desc.metallic_roughness_texture.sampler,
           get_texture_sampler(metallic_roughness_texture.index));
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
        OK(desc.normal_texture.image,
           get_or_create_texture_image(normal_texture.index, false));
        OK(desc.normal_texture.sampler,
           get_texture_sampler(normal_texture.index));
        desc.normal_texture.scale = normal_texture.scale;
      }
    }

    if (material.occlusionTexture.index >= 0) {
      warn("Occlusion textures and indirect lighting not implemented");
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

    return m_scene->create_material(desc).transform_error(get_error_string);
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
       m_scene
           ->create_mesh_instance({
               .mesh = mesh,
               .material = material,
           })
           .transform_error(get_error_string));
    m_scene->set_mesh_instance_transform(mesh_instance, transform);
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
  ren::IScene *m_scene = nullptr;
  std::unordered_map<GltfMeshDesc, ren::MeshId> m_mesh_cache;
  std::unordered_map<GltfImageDesc, ren::ImageId> m_image_cache;
  std::vector<ren::MaterialId> m_material_cache;
};

class ViewGlTFApp : public ImGuiApp {
public:
  ViewGlTFApp(const fs::path &path, unsigned scene)
      : ImGuiApp(fmt::format("View glTF: {}", path.c_str()).c_str()) {
    [&]() -> Result<void> {
      OK(tinygltf::Model model, load_gltf(path));
      SceneWalker scene_walker(std::move(model), get_scene());
      TRY_TO(scene_walker.walk(scene));
      OK(auto directional_light, get_scene().create_directional_light({
                                     .color = {1.0f, 1.0f, 1.0f},
                                     .illuminance = 100'000.0f,
                                     .origin = {0.0f, 0.0f, 1.0f},
                                 }));
      return {};
    }()
                 .transform_error(throw_error)
                 .value();
  }

  [[nodiscard]] static auto run(const fs::path &path, unsigned scene) -> int {
    return AppBase::run<ViewGlTFApp>(path, scene);
  }

protected:
  auto process_event(const SDL_Event &event) -> Result<void> override {
    TRY_TO(ImGuiApp::process_event(event));
    ImGuiIO &io = ImGui::GetIO();
    switch (event.type) {
    default:
      break;
    case SDL_MOUSEWHEEL: {
      if (imgui_wants_capture_mouse()) {
        break;
      }
      m_distance = m_distance *
                   glm::pow(2.0f, event.wheel.preciseY / m_zoom_sensitivity);
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
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
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
    if (ImGui::Begin("Scene graphics settings")) {
      draw_camera_imgui(m_camera_params);
      ImGui::End();
    }

    ren::IScene &scene = get_scene();

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

      scene.set_camera_transform(camera, {
                                             .position = position,
                                             .forward = forward,
                                             .up = up,
                                         });
      scene.set_camera_parameters(
          camera, {
                      .aperture = m_camera_params.aperture,
                      .shutter_time = 1.0f / m_camera_params.inv_shutter_time,
                      .iso = m_camera_params.iso,
                  });

      switch (m_camera_params.projection) {
      case PROJECTION_PERSPECTIVE: {
        scene.set_camera_perspective_projection(
            camera, {.hfov = glm::radians(m_camera_params.hfov)});
      } break;
      case PROJECTION_ORTHOGRAPHIC: {
        scene.set_camera_orthographic_projection(
            camera, {.width = m_camera_params.orthographic_width});
      } break;
      }

      float ec;
      switch (m_camera_params.exposure) {
      case ren::ExposureMode::Camera: {
        ec = m_camera_params.camera_exposure_comensation;
      } break;
      case ren::ExposureMode::Automatic: {
        ec = m_camera_params.automatic_exposure_compensation;
      } break;
      }
      scene.set_exposure({
          .mode = m_camera_params.exposure,
          .ec = ec,
      });
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

  auto path = result["file"].as<fs::path>();
  auto scene = result["scene"].as<unsigned>();

  return ViewGlTFApp::run(path, scene);
}
