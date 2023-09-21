#include "AppBase.hpp"

#include <boost/functional/hash.hpp>
#include <cxxopts.hpp>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tiny_gltf.h>

#include <cstdint>
#include <filesystem>

namespace chrono = std::chrono;
namespace fs = std::filesystem;

auto load_gltf(const fs::path &path) -> Result<tinygltf::Model> {
  tinygltf::TinyGLTF loader;
  tinygltf::Model model;
  std::string err;
  std::string warn;

  if (not fs::exists(path)) {
    bail("Failed to open file {}: doesn't exist", path);
  }

  bool ret = true;
  if (path.extension() == ".gltf") {
    ret = loader.LoadASCIIFromFile(&model, &err, &warn, path.string());
  } else if (path.extension() == ".glb") {
    ret = loader.LoadBinaryFromFile(&model, &err, &warn, path.string());
  } else {
    bail("Failed to load glTF file {}: invalid extension {}", path,
         path.extension());
  }

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
    -> Result<ren::Format> {
  if (pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
    if (components == 1) {
      return srgb ? REN_FORMAT_R8_SRGB : REN_FORMAT_R8_UNORM;
    }
    if (components == 2) {
      return srgb ? REN_FORMAT_RG8_SRGB : REN_FORMAT_RG8_UNORM;
    }
    if (components == 3) {
      return srgb ? REN_FORMAT_RGB8_SRGB : REN_FORMAT_RGB8_UNORM;
    }
    if (components == 4) {
      return srgb ? REN_FORMAT_RGBA8_SRGB : REN_FORMAT_RGBA8_UNORM;
    }
  }
  if (pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT and not srgb) {
    if (components == 1) {
      return REN_FORMAT_R16_UNORM;
    }
    if (components == 2) {
      return REN_FORMAT_RG16_UNORM;
    }
    if (components == 3) {
      return REN_FORMAT_RGB16_UNORM;
    }
    if (components == 4) {
      return REN_FORMAT_RGBA16_UNORM;
    }
  }
  bail("Unknown format: {}/{}, sRGB: {}", components, pixel_type, srgb);
}

auto get_sampler_wrap_mode(int mode) -> Result<ren::WrappingMode> {
  switch (mode) {
  default:
    bail("Unknown sampler wrapping mode {}", mode);
  case TINYGLTF_TEXTURE_WRAP_REPEAT:
    return REN_WRAPPING_MODE_REPEAT;
  case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
    return REN_WRAPPING_MODE_CLAMP_TO_EDGE;
  case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
    return REN_WRAPPING_MODE_MIRRORED_REPEAT;
  }
}

auto get_sampler(const tinygltf::Sampler &sampler) -> Result<ren::Sampler> {
  ren::Filter mag_filter, min_filter, mip_filter;
  switch (sampler.magFilter) {
  default:
    bail("Unknown sampler magnification filter {}", sampler.magFilter);
  case TINYGLTF_TEXTURE_FILTER_LINEAR:
    mag_filter = REN_FILTER_LINEAR;
    break;
  case TINYGLTF_TEXTURE_FILTER_NEAREST:
    mag_filter = REN_FILTER_NEAREST;
    break;
  }
  switch (sampler.minFilter) {
  default:
    bail("Unknown sampler magnification filter {}", sampler.magFilter);
  case TINYGLTF_TEXTURE_FILTER_LINEAR:
    bail("Linear minification filter not implemented");
  case TINYGLTF_TEXTURE_FILTER_NEAREST:
    bail("Nearest minification filter not implemented");
  case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
    min_filter = REN_FILTER_LINEAR;
    mip_filter = REN_FILTER_LINEAR;
    break;
  case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
    min_filter = REN_FILTER_LINEAR;
    mip_filter = REN_FILTER_NEAREST;
    break;
  case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
    min_filter = REN_FILTER_NEAREST;
    mip_filter = REN_FILTER_LINEAR;
    break;
  case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
    min_filter = REN_FILTER_NEAREST;
    mip_filter = REN_FILTER_NEAREST;
    break;
  }
  OK(ren::WrappingMode wrap_u, get_sampler_wrap_mode(sampler.wrapS));
  OK(ren::WrappingMode wrap_v, get_sampler_wrap_mode(sampler.wrapT));
  return ren::Sampler{
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
  static auto operator()(const GltfMeshDesc &desc) -> size_t {
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
  static auto operator()(const GltfImageDesc &desc) -> size_t {
    return std::hash<int>()(std::bit_cast<int>(desc));
  }
};

class SceneWalker {
public:
  SceneWalker(tinygltf::Model model, ren::Scene &scene) {
    m_model = std::move(model);
    m_scene = &scene;
  }

  auto walk(int scene) -> Result<void> {
    if (not m_model.extensionsRequired.empty()) {
      bail("Required glTF extensions not supported: {}",
           m_model.extensionsRequired);
    }

    if (scene >= m_model.scenes.size()) {
      bail("Scene index {} out of bounds", scene);
    }

    TRY_TO(walk_scene(m_model.scenes[scene]));
    if (not m_mesh_insts.empty()) {
      m_scene->set_mesh_inst_matrices(
          m_mesh_insts, std::span(reinterpret_cast<const RenMatrix4x4 *>(
                                      m_mesh_inst_transforms.data()),
                                  m_mesh_inst_transforms.size()));
    }

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

  auto create_mesh(const GltfMeshDesc &desc) -> Result<ren::MeshID> {
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

    if (tangents) {
      fmt::println(stderr, "Warn: ignoring tangents");
    }

    OK(auto colors_data, [&] -> Result<std::vector<glm::vec4>> {
      if (!colors) {
        return {};
      }
      if (colors->type == TINYGLTF_TYPE_VEC3) {
        if (colors->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
          fmt::println(
              stderr,
              "Warn: converting colors from RGB8_SFLOAT to RGBA32_SFLOAT");
          return get_accessor_data<glm::vec3>(
              *colors, [](glm::vec3 color) { return glm::vec4(color, 1.0f); });
        }
        if (colors->normalized) {
          if (colors->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            fmt::println(
                stderr,
                "Warn: converting colors from RGB8_UNORM to RGBA32_SFLOAT");
            return get_accessor_data<glm::u8vec3>(
                *colors, [](glm::u8vec3 color) {
                  return glm::vec4(glm::unpackUnorm<float>(color), 1.0f);
                });
          }
          if (colors->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            fmt::println(
                stderr,
                "Warn: converting colors from RGB16_UNORM to RGBA32_SFLOAT");
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
            fmt::println(
                stderr,
                "Warn: converting colors from RGBA8_UNORM to RGBA32_SFLOAT");
            return get_accessor_data<glm::u8vec4>(
                *colors, [](glm::u8vec4 color) {
                  return glm::unpackUnorm<float>(color);
                });
          }
          if (colors->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            fmt::println(
                stderr,
                "Warn: converting colors from RGBA16_UNORM to RGBA32_SFLOAT");
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

    OK(auto uvs_data, [&] -> Result<std::vector<glm::vec2>> {
      if (!uvs) {
        return {};
      }
      if (uvs->type == TINYGLTF_TYPE_VEC2) {
        if (uvs->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
          return get_accessor_data<glm::vec2>(*uvs);
        }
        if (uvs->normalized) {
          if (uvs->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            fmt::println(stderr,
                         "Warn: converting UVs from RG8_UNORM to RG32_SFLOAT");
            return get_accessor_data<glm::u8vec2>(*uvs, [](glm::u8vec2 color) {
              return glm::unpackUnorm<float>(color);
            });
          }
          if (uvs->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            fmt::println(stderr,
                         "Warn: converting UVs from RG16_UNORM to RG32_SFLOAT");
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

    OK(auto indices_data, [&] -> Result<std::vector<uint32_t>> {
      if (!indices) {
        std::vector<uint32_t> indices_data(positions_data.size());
        std::ranges::iota(indices_data, 0);
        return indices_data;
      }
      if (not indices->normalized and indices->type == TINYGLTF_TYPE_SCALAR) {
        if (indices->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
          fmt::println(stderr,
                       "Warn: converting indices from R8_UINT to R32_UINT");
          return get_accessor_data<uint8_t>(
              *indices, [](uint8_t index) -> uint32_t { return index; });
        }
        if (indices->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
          fmt::println(stderr,
                       "Warn: converting indices from R16_UINT to R32_UINT");
          return get_accessor_data<uint16_t>(
              *indices, [](uint16_t index) -> uint32_t { return index; });
        }
        if (indices->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
          return get_accessor_data<uint32_t>(*indices);
        }
      }
      bail("Invalid primitive indices format: "
           "{}/{}, normalized: {}",
           indices->componentType, indices->type, indices->normalized);
    }());

    return m_scene
        ->create_mesh({
            .num_vertices = uint32_t(positions_data.size()),
            .num_indices = uint32_t(indices_data.size()),
            .positions = (const RenVector3 *)positions_data.data(),
            .colors = colors_data.empty()
                          ? nullptr
                          : (const RenVector4 *)colors_data.data(),
            .normals = (const RenVector3 *)normals_data.data(),
            .uvs = uvs_data.empty() ? nullptr
                                    : (const RenVector2 *)uvs_data.data(),
            .indices = indices_data.data(),
        })
        .transform_error(get_error_string);
  }

  auto get_or_create_mesh(const tinygltf::Primitive &primitive)
      -> Result<ren::MeshID> {
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
    ren::MeshID &mesh = m_mesh_cache[desc];
    if (!mesh) {
      OK(ren::MeshID mesh, create_mesh(desc));
    }
    return mesh;
  }

  auto create_image(const GltfImageDesc &desc) -> Result<ren::ImageID> {
    const tinygltf::Image &image = m_model.images[desc.index];
    assert(image.image.size() ==
           image.width * image.height * image.component * image.bits / 8);
    OK(ren::Format format,
       get_image_format(image.component, image.pixel_type, desc.srgb));
    return m_scene
        ->create_image({
            .format = format,
            .width = unsigned(image.width),
            .height = unsigned(image.height),
            .data = image.image.data(),
        })
        .transform_error(get_error_string);
  }

  auto get_or_create_image(int index, bool srgb) -> Result<ren::ImageID> {
    GltfImageDesc desc = {
        .index = index,
        .srgb = srgb,
    };
    ren::ImageID &image = m_image_cache[desc];
    if (!image) {
      OK(image, create_image(desc));
    }
    return image;
  }

  auto get_texture_sampler(int texture) const -> Result<ren::Sampler> {
    int sampler = m_model.textures[texture].sampler;
    if (sampler < 0) {
      bail("Default sampler not implemented");
    }
    return get_sampler(m_model.samplers[sampler]);
  }

  auto create_material(int index) -> Result<ren::MaterialID> {
    const tinygltf::Material &material = m_model.materials[index];
    ren::MaterialDesc desc = {};

    assert(material.pbrMetallicRoughness.baseColorFactor.size() == 4);
    glm::vec4 base_color_factor =
        glm::make_vec4(material.pbrMetallicRoughness.baseColorFactor.data());
    std::memcpy(desc.base_color_factor, &base_color_factor,
                sizeof(base_color_factor));

    const tinygltf::TextureInfo &base_color_texture =
        material.pbrMetallicRoughness.baseColorTexture;
    if (base_color_texture.index >= 0) {
      if (base_color_texture.texCoord > 0) {
        bail("Only one texture coordinate set supported");
      }
      OK(desc.color_tex.image,
         get_or_create_image(base_color_texture.index, true));
      OK(desc.color_tex.sampler, get_texture_sampler(base_color_texture.index));
    }

    if (material.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
      bail("Metallic-roughness textures are not implemented");
    }

    desc.metallic_factor = material.pbrMetallicRoughness.metallicFactor;
    desc.roughness_factor = material.pbrMetallicRoughness.roughnessFactor;

    if (material.normalTexture.index >= 0) {
      bail("Normal mapping not implemented");
    }
    if (material.occlusionTexture.index >= 0) {
      bail("Occlusion textures not implemented");
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

  auto get_or_create_material(int index) -> Result<ren::MaterialID> {
    assert(index >= 0);
    if (index >= m_material_cache.size()) {
      m_material_cache.resize(index + 1);
    }
    ren::MaterialID &mat = m_material_cache[index];
    if (!mat) {
      OK(mat, create_material(index));
    }
    return mat;
  }

  auto create_mesh_instance(const tinygltf::Primitive &primitive,
                            const glm::mat4 &transform)
      -> Result<ren::MeshInstID> {
    OK(ren::MeshID mesh, get_or_create_mesh(primitive));
    OK(ren::MaterialID material, get_or_create_material(primitive.material));
    OK(ren::MeshInstID mesh_inst, m_scene
                                      ->create_mesh_inst({
                                          .mesh = mesh,
                                          .material = material,
                                          .casts_shadows = true,
                                      })
                                      .transform_error(get_error_string));
    m_mesh_insts.push_back(mesh_inst);
    m_mesh_inst_transforms.push_back(transform);
    return mesh_inst;
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
    glm::mat4 transform = parent_transform * get_node_local_transform(node);

    if (node.mesh >= 0) {
      const tinygltf::Mesh &mesh = m_model.meshes[node.mesh];
      for (const tinygltf::Primitive &primitive : mesh.primitives) {
        auto res = create_mesh_instance(primitive, transform);
        if (not res.has_value()) {
          fmt::println(stderr,
                       "Warn: failed to create mesh instance for mesh {} "
                       "primitive {} in node {}: {}",
                       node.mesh, &primitive - &mesh.primitives[0],
                       &node - &m_model.nodes[0], res.error());
        }
      }
    }

    for (int child : node.children) {
      TRY_TO(walk_node(m_model.nodes[child], transform));
    }

    return {};
  };

  auto walk_scene(const tinygltf::Scene &scene) -> Result<void> {
    auto transform = glm::identity<glm::mat4>();
    for (int node : scene.nodes) {
      TRY_TO(walk_node(m_model.nodes[node], transform));
    }
    return {};
  }

private:
  tinygltf::Model m_model;
  ren::Scene *m_scene = nullptr;
  std::unordered_map<GltfMeshDesc, ren::MeshID> m_mesh_cache;
  std::unordered_map<GltfImageDesc, ren::ImageID> m_image_cache;
  std::vector<ren::MaterialID> m_material_cache;
  std::vector<ren::MeshInstID> m_mesh_insts;
  std::vector<glm::mat4> m_mesh_inst_transforms;
};

class ViewGlTFApp : public AppBase {
public:
  ViewGlTFApp(const fs::path &path, unsigned scene)
      : AppBase(fmt::format("View glTF: {}", path).c_str()) {
    [&]() -> Result<void> {
      OK(tinygltf::Model model, load_gltf(path));
      SceneWalker scene_walker(std::move(model), get_scene());
      TRY_TO(scene_walker.walk(scene));
      OK(auto dir_light, get_scene().create_dir_light({
                             .color = {1.0f, 1.0f, 1.0f},
                             .illuminance = 25'000.0f,
                             .origin = {0.0f, 0.0f, 1.0f},
                         }));
      return {};
    }()
                 .transform_error(throw_error);
  }

  [[nodiscard]] static auto run(const fs::path &path, unsigned scene) -> int {
    return AppBase::run<ViewGlTFApp>(path, scene);
  }

protected:
  auto process_event(const SDL_Event &e) -> Result<void> override {
    switch (e.type) {
    default:
      break;
    case SDL_MOUSEWHEEL: {
      m_distance =
          m_distance * glm::pow(2.0f, e.wheel.preciseY / m_zoom_sensitivity);
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

  auto iterate(unsigned width, unsigned height, chrono::nanoseconds dt_ns)
      -> Result<void> override {
    float dt = chrono::duration_cast<chrono::duration<float>>(dt_ns).count();

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

    ren::Scene &scene = get_scene();
    ren::CameraDesc desc = {
        .width = width,
        .height = height,
        .exposure_compensation = 3.0f,
        .exposure_mode = REN_EXPOSURE_MODE_AUTOMATIC,
    };
    std::memcpy(desc.position, &position, sizeof(desc.position));
    std::memcpy(desc.forward, &forward, sizeof(desc.forward));
    std::memcpy(desc.up, &up, sizeof(desc.up));
    desc.set_projection(ren::PerspectiveProjection{
        .hfov = glm::radians(90.0f),
    });
    TRY_TO(scene.set_camera(desc));

    return {};
  }

private:
  SDL_Scancode m_pitch_up_key = SDL_SCANCODE_W;
  SDL_Scancode m_pitch_down_key = SDL_SCANCODE_S;
  SDL_Scancode m_yaw_left_key = SDL_SCANCODE_A;
  SDL_Scancode m_yaw_right_key = SDL_SCANCODE_D;

  float m_pitch_speed = glm::radians(45.0f);
  float m_pitch = 0.0f;

  float m_yaw_speed = -glm::radians(45.0f);
  float m_yaw = 0.0f;

  float m_zoom_sensitivity = -25.0f;
  float m_distance = 3.0f;
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
