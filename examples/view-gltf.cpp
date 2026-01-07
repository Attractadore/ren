#include "ImGuiApp.hpp"
#include "ren/baking/image.hpp"
#include "ren/baking/mesh.hpp"
#include "ren/core/Algorithm.hpp"
#include "ren/core/Chrono.hpp"
#include "ren/core/CmdLine.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/Job.hpp"
#include "ren/core/Span.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/color_space.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

#include "ren/core/GLTF.hpp"

using namespace ren;

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

ren::WrappingMode get_sampler_wrap_mode(int mode) {
  switch (mode) {
  default:
    fmt::println(stderr, "Unknown sampler wrapping mode {}", mode);
    return ren::WrappingMode::ClampToEdge;
  case GLTF_TEXTURE_WRAP_REPEAT:
    return ren::WrappingMode::Repeat;
  case GLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
    return ren::WrappingMode::ClampToEdge;
  case GLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
    return ren::WrappingMode::MirroredRepeat;
  }
}

ren::SamplerDesc get_sampler(const GltfSampler &sampler) {
  ren::Filter mag_filter, min_filter, mip_filter;
  switch (sampler.mag_filter) {
  default:
    fmt::println(stderr, "Unknown sampler magnification filter {}",
                 sampler.mag_filter);
    mag_filter = ren::Filter::Linear;
    break;
  case GLTF_TEXTURE_FILTER_LINEAR:
    mag_filter = ren::Filter::Linear;
    break;
  case GLTF_TEXTURE_FILTER_NEAREST:
    mag_filter = ren::Filter::Nearest;
    break;
  }
  switch (sampler.min_filter) {
  default:
    fmt::println(stderr, "Unknown sampler minification filter {}",
                 sampler.mag_filter);
    min_filter = ren::Filter::Linear;
    mip_filter = ren::Filter::Linear;
    break;
  case GLTF_TEXTURE_FILTER_LINEAR:
    fmt::println(stderr, "Linear minification filter not implemented");
    min_filter = ren::Filter::Linear;
    mip_filter = ren::Filter::Linear;
    break;
  case GLTF_TEXTURE_FILTER_NEAREST:
    fmt::println(stderr, "Nearest minification filter not implemented");
    min_filter = ren::Filter::Nearest;
    mip_filter = ren::Filter::Nearest;
    break;
  case GLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
    min_filter = ren::Filter::Linear;
    mip_filter = ren::Filter::Linear;
    break;
  case GLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
    min_filter = ren::Filter::Linear;
    mip_filter = ren::Filter::Nearest;
    break;
  case GLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
    min_filter = ren::Filter::Nearest;
    mip_filter = ren::Filter::Linear;
    break;
  case GLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
    min_filter = ren::Filter::Nearest;
    mip_filter = ren::Filter::Nearest;
    break;
  }
  ren::WrappingMode wrap_u = get_sampler_wrap_mode(sampler.wrap_s);
  ren::WrappingMode wrap_v = get_sampler_wrap_mode(sampler.wrap_t);
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
  GltfPrimitive gltf_primitive;
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
  SceneWalker(const Gltf &gltf, ren::NotNull<ren::Arena *> load_arena,
              ren::NotNull<ren::Arena *> frame_arena,
              ren::NotNull<ren::Scene *> scene) {
    m_gltf = gltf;
    m_load_arena = load_arena;
    m_frame_arena = frame_arena;
    m_scene = scene;
  }

  void walk(ren::u32 scene) {
    if (not m_gltf.animations.is_empty()) {
      warn("Ignoring {} animations", m_gltf.animations.size());
    }

    if (not m_gltf.skins.is_empty()) {
      warn("Ignoring {} skins", m_gltf.skins.size());
    }

    if (not m_gltf.cameras.is_empty()) {
      warn("Ignoring {} cameras", m_gltf.cameras.size());
    }

    if (scene >= m_gltf.scenes.size()) {
      fmt::println(stderr, "Scene index {} out of bounds", scene);
      exit(EXIT_FAILURE);
    }

    m_default_material =
        ren::create_material(m_frame_arena, m_scene, MaterialCreateInfo{});

    walk_scene(m_gltf.scenes[scene]);
  }

private:
  template <typename T>
  ren::Span<const T> get_accessor_data(i32 accessor_index) const {
    if (accessor_index == -1) {
      return {};
    }
    const GltfAccessor &accessor = m_gltf.accessors[accessor_index];
    const GltfBufferView &view = m_gltf.buffer_views[accessor.buffer_view];
    ren::Span<const std::byte> src_data = m_gltf.buffers[view.buffer].bytes;
    src_data = src_data.subspan(view.byte_offset, view.byte_length);
    ren_assert(accessor.byte_offset == 0);
    ren_assert(view.byte_stride == 0);
    return Span((const T *)src_data.data(), accessor.count);
  }

  ren::Handle<ren::Mesh> create_mesh(const GltfPrimitive &primitive) {
    ren::ScratchArena scratch;

    GltfAttribute positions = *gltf_find_attribute_by_semantic(
        primitive, GltfAttributeSemantic::POSITION);
    GltfAttribute normals = *gltf_find_attribute_by_semantic(
        primitive, GltfAttributeSemantic::NORMAL);
    Optional<GltfAttribute> tangents = gltf_find_attribute_by_semantic(
        primitive, GltfAttributeSemantic::TANGENT);
    Optional<GltfAttribute> colors = gltf_find_attribute_by_semantic(
        primitive, GltfAttributeSemantic::COLOR);
    Optional<GltfAttribute> uvs = gltf_find_attribute_by_semantic(
        primitive, GltfAttributeSemantic::TEXCOORD);

    ren::Span<const glm::vec3> positions_data =
        get_accessor_data<glm::vec3>(positions.accessor);
    ren::Span<const glm::vec3> normals_data =
        get_accessor_data<glm::vec3>(normals.accessor);
    ren::Span<const glm::vec4> tangents_data =
        get_accessor_data<glm::vec4>(tangents ? tangents->accessor : -1);
    Span<const glm::vec2> uv_data =
        get_accessor_data<glm::vec2>(uvs ? uvs->accessor : -1);
    ren::Span<const glm::vec4> colors_data =
        get_accessor_data<glm::vec4>(colors ? colors->accessor : -1);
    Span<const u32> indices_data = get_accessor_data<u32>(primitive.indices);

    ren::Blob blob = ren::bake_mesh_to_memory(
        scratch, {
                     .num_vertices = positions_data.m_size,
                     .positions = positions_data.m_data,
                     .normals = normals_data.m_data,
                     .tangents = tangents_data.m_data,
                     .uvs = uv_data.m_data,
                     .colors = colors_data.m_data,
                     .indices = indices_data,
                 });
    return ren::create_mesh(m_frame_arena, m_scene, blob.data, blob.size);
  }

  ren::Handle<ren::Mesh> get_or_create_mesh(const GltfPrimitive &primitive) {
    const MeshCacheItem *cached =
        ren::find_if(ren::Span(m_mesh_cache), [&](const MeshCacheItem &item) {
          return item.gltf_primitive == primitive;
        });
    if (cached) {
      return cached->handle;
    }

    auto warn_unused_attribute = [&](GltfAttributeSemantic semantic,
                                     i32 set_index = 0) {
      for (;; ++set_index) {
        if (!gltf_find_attribute_by_semantic(primitive, semantic, set_index)) {
          break;
        }
        warn("Ignoring primitive attribute {}_{}", semantic, set_index);
      }
    };
    warn_unused_attribute(GltfAttributeSemantic::TEXCOORD, 1);
    warn_unused_attribute(GltfAttributeSemantic::COLOR, 1);
    warn_unused_attribute(GltfAttributeSemantic::JOINTS, 0);
    warn_unused_attribute(GltfAttributeSemantic::WEIGHTS, 0);
    if (primitive.mode != GLTF_TOPOLOGY_TRIANGLES) {
      fmt::println(stderr, "Unsupported primitive mode {}", primitive.mode);
      return ren::NullHandle;
    }
    ren::Handle<ren::Mesh> mesh = create_mesh(primitive);
    m_mesh_cache.push(m_load_arena, {primitive, mesh});
    return mesh;
  }

  ren::TextureInfo get_image_info(int image, bool srgb = false) {
    const GltfImage &gltf_image = m_gltf.images[image];
    return {
        .format = srgb ? TinyImageFormat_R8G8B8A8_SRGB
                       : TinyImageFormat_R8G8B8A8_UNORM,
        .width = unsigned(gltf_image.width),
        .height = unsigned(gltf_image.height),
        .data = gltf_image.pixels.data(),
    };
  }

  ren::SamplerDesc get_texture_sampler(int texture) const {
    int sampler = m_gltf.textures[texture].sampler;
    if (sampler < 0) {
      fmt::println(stderr, "Default sampler not implemented");
      return {};
    }
    return get_sampler(m_gltf.samplers[sampler]);
  }

  ren::Handle<ren::Material> create_material(int index) {
    const GltfMaterial &material = m_gltf.materials[index];
    ren::MaterialCreateInfo desc = {};

    desc.base_color_factor = material.pbr_metallic_roughness.base_color_factor;

    {
      const GltfTextureInfo &base_color_texture =
          material.pbr_metallic_roughness.base_color_texture;
      if (base_color_texture.index >= 0) {
        if (base_color_texture.tex_coord > 0) {
          fmt::println(stderr,
                       "Unsupported base color texture coordinate set {}",
                       base_color_texture.tex_coord);
          return ren::NullHandle;
        }
        int src = m_gltf.textures[base_color_texture.index].source;
        const ImageCacheItem *cached = ren::find_if(
            ren::Span(m_color_image_cache),
            [&](const ImageCacheItem &item) { return item.id == src; });
        if (cached) {
          desc.base_color_texture.image = cached->handle;
        } else {
          ren::ScratchArena scratch;
          ren::TextureInfo texture_info = get_image_info(src, true);
          auto blob = ren::bake_color_map_to_memory(scratch, texture_info);
          ren::Handle<ren::Image> image =
              create_image(m_frame_arena, m_scene, blob.data, blob.size);
          m_color_image_cache.push(m_load_arena, {src, image});
          desc.base_color_texture.image = image;
        }
        desc.base_color_texture.sampler =
            get_texture_sampler(base_color_texture.index);
      }
    }

    desc.metallic_factor = material.pbr_metallic_roughness.metallic_factor;
    desc.roughness_factor = material.pbr_metallic_roughness.roughness_factor;

    {
      const GltfTextureInfo &metallic_roughness_texture =
          material.pbr_metallic_roughness.metallic_roughness_texture;
      const GltfOcclusionTextureInfo &occlusion_texture =
          material.occlusion_texture;
      if (metallic_roughness_texture.index >= 0) {
        if (metallic_roughness_texture.tex_coord > 0) {
          fmt::println(
              stderr,
              "Unsupported metallic-roughness texture coordinate set {}",
              metallic_roughness_texture.tex_coord);
          return ren::NullHandle;
        }
        int roughness_metallic_src =
            m_gltf.textures[metallic_roughness_texture.index].source;
        int occlusion_src = -1;
        if (occlusion_texture.index >= 0) {
          if (occlusion_texture.tex_coord > 0) {
            fmt::println(stderr,
                         "Unsupported occlusion texture coordinate set {}",
                         occlusion_texture.tex_coord);
            return ren::NullHandle;
          }
          occlusion_src = m_gltf.textures[occlusion_texture.index].source;
        }
        const OrmImageCacheItem *cached = ren::find_if(
            ren::Span(m_orm_image_cache), [&](const OrmImageCacheItem &item) {
              return item.rm_id == roughness_metallic_src and
                     item.o_id == occlusion_src;
            });
        if (cached) {
          desc.orm_texture.image = cached->handle;
        } else {
          ren::ScratchArena scratch;
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
      const GltfNormalTextureInfo &normal_texture = material.normal_texture;
      if (normal_texture.index >= 0) {
        if (normal_texture.tex_coord > 0) {
          fmt::println(stderr, "Unsupported normal texture coordinate set {}",
                       normal_texture.tex_coord);
          return ren::NullHandle;
        }
        int src = m_gltf.textures[normal_texture.index].source;
        const ImageCacheItem *cached = ren::find_if(
            ren::Span(m_normal_image_cache),
            [&](const ImageCacheItem &item) { return item.id == src; });
        if (cached) {
          desc.normal_texture.image = cached->handle;
        } else {
          ren::ScratchArena scratch;
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

    glm::vec3 emissive = material.emissive_factor;
    if (material.emissive_texture.index >= 0 or
        emissive != glm::vec3{0.0f, 0.0f, 0.0f}) {
      warn("Emissive materials not implemented");
    }

    if (material.alphaMode != GLTF_ALPHA_MODE_OPAQUE) {
      warn("Translucent materials not implemented");
    }

    if (material.doubleSided) {
      warn("Double sided materials not implemented");
    }

    return ren::create_material(m_frame_arena, m_scene, desc);
  }

  ren::Handle<ren::Material> get_or_create_material(int index) {
    if (index == -1) {
      return m_default_material;
    }
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
  create_mesh_instance(const GltfPrimitive &primitive,
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

  void walk_node(const GltfNode &node, const glm::mat4 &parent_transform) {
    int node_index = &node - m_gltf.nodes.data();
    glm::mat4 transform = parent_transform * node.matrix;

    if (node.mesh >= 0) {
      const GltfMesh &mesh = m_gltf.meshes[node.mesh];
      for (const GltfPrimitive &primitive : mesh.primitives) {
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

    for (int child : node.children) {
      walk_node(m_gltf.nodes[child], transform);
    }
  };

  void walk_scene(const GltfScene &scene) {
    auto transform = glm::mat4_cast(
        glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    for (int node : scene.nodes) {
      walk_node(m_gltf.nodes[node], transform);
    }
  }

private:
  Gltf m_gltf;
  ren::Arena *m_load_arena = nullptr;
  ren::Arena *m_frame_arena = nullptr;
  ren::Scene *m_scene = nullptr;
  ren::DynamicArray<MeshCacheItem> m_mesh_cache;
  ren::DynamicArray<ImageCacheItem> m_color_image_cache;
  ren::DynamicArray<OrmImageCacheItem> m_orm_image_cache;
  ren::DynamicArray<ImageCacheItem> m_normal_image_cache;
  Handle<Material> m_default_material;
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

    auto start = ren::clock();
    Result<Gltf, GltfErrorInfo> gltf = load_gltf(
        scratch, {
                     .path = options.path,
                     .load_buffers = true,
                     .load_images = true,
                     .load_image_callback = gltf_stbi_callback,
                     .optimize_flags = GltfOptimize::NormalizeSceneBounds |
                                       GltfOptimize::ConvertMeshAccessors,
                 });
    auto end = ren::clock();
    if (!gltf) {
      fmt::println(stderr, "{}", gltf.error().message);
      exit(EXIT_FAILURE);
    }
    log("Loaded scene in {:.3f}s", (end - start) / 1e9);

    SceneWalker scene_walker(*gltf, scratch, &m_frame_arena, get_scene());
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
  ren::ScratchArena::init_for_thread();
  launch_job_server();
  ren::ScratchArena scratch;

  // clang-format off
  ren::CmdLineOption options[] = {
      {OPTION_FILE, ren::CmdLinePath, "file", 0, "path to glTF file", ren::CmdLinePositional},
      {OPTION_SCENE, ren::CmdLineUInt, "scene", 0, "index of scene to view"},
      {OPTION_ENV_MAP, ren::CmdLinePath, "env-map", 0, "path to environment map"},
      {OPTION_HELP, ren::CmdLineFlag, "help", 'h', "show this message"},
  };
  // clang-format on
  ren::ParsedCmdLineOption parsed[OPTION_COUNT];
  bool success = parse_cmd_line(scratch, argv, options, parsed);
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
