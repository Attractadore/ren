#include "Scene.hpp"
#include "Camera.inl"
#include "Formats.hpp"
#include "ImGuiConfig.hpp"
#include "Passes.hpp"
#include "Support/Errors.hpp"
#include "Swapchain.hpp"
#include "glsl/Batch.hpp"

#include <meshoptimizer.h>
#include <mikktspace.h>
#include <range/v3/algorithm.hpp>
#include <range/v3/numeric.hpp>
#include <range/v3/range.hpp>

namespace ren {

auto Hash<SamplerDesc>::operator()(const SamplerDesc &sampler) const noexcept
    -> usize {
  usize seed = 0;
  seed = hash_combine(seed, sampler.mag_filter);
  seed = hash_combine(seed, sampler.min_filter);
  seed = hash_combine(seed, sampler.mipmap_filter);
  seed = hash_combine(seed, sampler.wrap_u);
  seed = hash_combine(seed, sampler.wrap_v);
  return seed;
}

SceneImpl::SceneImpl(SwapchainImpl &swapchain) {
  m_persistent_descriptor_set_layout =
      create_persistent_descriptor_set_layout();
  std::tie(m_persistent_descriptor_pool, m_persistent_descriptor_set) =
      allocate_descriptor_pool_and_set(m_persistent_descriptor_set_layout);

  m_texture_allocator = std::make_unique<TextureIdAllocator>(
      m_persistent_descriptor_set, m_persistent_descriptor_set_layout);

  m_render_graph =
      std::make_unique<RenderGraph>(swapchain, *m_texture_allocator);

  m_pipelines = load_pipelines(m_arena, m_persistent_descriptor_set_layout);

  // TODO: delete when Clang implements constexpr std::bit_cast for structs
  // with bitfields
#define error "C handles can't be directly converted to SlotMap keys"
#if !BOOST_COMP_CLANG
  static_assert(std::bit_cast<u32>(SlotMapKey()) == 0, error);
#else
  ren_assert_msg(std::bit_cast<u32>(SlotMapKey()) == 0, error);
#endif
#undef error

  m_batch_max_counts.resize(glsl::NUM_BATCHES);
  m_batch_offsets.resize(glsl::NUM_BATCHES);
}

void SceneImpl::next_frame() {
  m_cmd_allocator.next_frame();
  m_texture_allocator->next_frame();
}

auto SceneImpl::create_mesh(const MeshDesc &desc) -> MeshId {
  u32 num_vertices = desc.positions.size();
  u32 num_indices = desc.indices.empty() ? num_vertices : desc.indices.size();

  ren_assert(num_vertices > 0);
  ren_assert(desc.normals.size() == num_vertices);
  ren_assert(num_indices % 3 == 0);
  ren_assert_msg(num_vertices <= glsl::NUM_VERTEX_POOL_VERTICES,
                 "Vertex pool overflow");
  ren_assert_msg(num_indices <= glsl::NUM_VERTEX_POOL_INDICES,
                 "Index pool overflow");

  MeshAttributeFlags attributes;
  if (not desc.tangents.empty()) {
    ren_assert(desc.tangents.size() == num_vertices);
    attributes |= MeshAttribute::Tangent;
  }
  if (not desc.tex_coords.empty()) {
    ren_assert(desc.tex_coords.size() == num_vertices);
    attributes |= MeshAttribute::Tangent;
    attributes |= MeshAttribute::UV;
  }
  if (not desc.colors.empty()) {
    ren_assert(desc.colors.size() == num_vertices);
    attributes |= MeshAttribute::Color;
  }

  Mesh mesh = {.attributes = attributes};

  auto positions = desc.positions | ranges::to<Vector>;
  auto normals = desc.normals | ranges::to<Vector>;
  auto tangents = desc.tangents | ranges::to<Vector>;
  auto uvs = desc.tex_coords | ranges::to<Vector>;
  auto colors = desc.colors | ranges::to<Vector>;

  Vector<u32> remap;
  auto remap_streams = [&] {
    auto remap_stream = [&]<typename T>(Vector<T> &stream) {
      meshopt_remapVertexBuffer(stream.data(), stream.data(), stream.size(),
                                sizeof(T), remap.data());
      stream.resize(num_vertices);
    };
    remap_stream(positions);
    remap_stream(normals);
    if (not tangents.empty()) {
      remap_stream(tangents);
    }
    if (not uvs.empty()) {
      remap_stream(uvs);
    }
    if (not colors.empty()) {
      remap_stream(colors);
    }
  };

  constexpr float LOD_THRESHOLD = 0.75f;
  constexpr float LOD_ERROR = 0.001f;

  Vector<u32> indices;
  auto generate_index_buffer = [&](Span<const u32> init_indices) {
    StaticVector<meshopt_Stream, 5> streams;
    auto add_stream = [&]<typename T>(Vector<T> &stream) {
      streams.push_back({
          .data = stream.data(),
          .size = sizeof(T),
          .stride = sizeof(T),
      });
    };
    add_stream(positions);
    add_stream(normals);
    if (not tangents.empty()) {
      add_stream(tangents);
    }
    if (not uvs.empty()) {
      add_stream(uvs);
    }
    if (not colors.empty()) {
      add_stream(colors);
    }
    const u32 *init = init_indices.data();
    if (init_indices.empty()) {
      init = nullptr;
      num_indices = num_vertices;
    } else {
      num_indices = init_indices.size();
    }
    remap.resize(num_vertices);
    num_vertices = meshopt_generateVertexRemapMulti(
        remap.data(), init, num_indices, num_vertices, streams.data(),
        streams.size());
    indices.resize(num_indices);
    meshopt_remapIndexBuffer(indices.data(), init, num_indices, remap.data());
    remap_streams();
  };

  // (Re)generate index buffer to remove duplicate vertices for LOD generation
  // to work correctly

  indices.reserve(num_indices * 1.0f / (1.0f - LOD_THRESHOLD) + 1);
  generate_index_buffer(desc.indices);

  // Generate LODs

  {
    mesh.lods.push_back({.num_indices = num_indices});

    // Skip LOD generation if tangents are given to avoid generating triangles
    // with inconsistent tangent space handedness
    if (tangents.empty()) {
      Vector<u32> lod_indices(num_indices);
      while (mesh.lods.size() < glsl::MAX_NUM_LODS) {
        u32 num_prev_lod_indices = mesh.lods.back().num_indices;

        auto num_target_indices =
            std::max<u32>(num_prev_lod_indices * LOD_THRESHOLD, 3);
        num_target_indices -= num_target_indices % 3;

        u32 num_lod_indices = meshopt_simplify(
            lod_indices.data(), indices.data(), num_prev_lod_indices,
            (const float *)positions.data(), num_vertices, sizeof(glm::vec3),
            num_target_indices, LOD_ERROR, 0, nullptr);
        if (num_lod_indices > num_target_indices) {
          break;
        }
        lod_indices.resize(num_lod_indices);

        // Insert coarser LODs in front for vertex fetch optimization
        indices.insert(indices.begin(), lod_indices.begin(), lod_indices.end());

        mesh.lods.push_back({.num_indices = num_lod_indices});
      }

      for (usize lod = mesh.lods.size() - 1; lod > 0; --lod) {
        mesh.lods[lod - 1].base_index =
            mesh.lods[lod].base_index + mesh.lods[lod].num_indices;
      }
    }
  }
  num_indices = indices.size();

  // Generate tangents

  if (not uvs.empty() and tangents.empty()) {
    auto unindex_stream = [&]<typename T>(Vector<T> &stream) {
      Vector<T> unindexed_stream =
          indices | map([&](u32 index) { return stream[index]; });
      std::swap(stream, unindexed_stream);
    };
    num_vertices = num_indices;
    unindex_stream(positions);
    unindex_stream(normals);
    tangents.resize(num_vertices);
    unindex_stream(uvs);
    if (not colors.empty()) {
      unindex_stream(colors);
    }

    struct Context {
      size_t num_faces = 0;
      const glm::vec3 *positions = nullptr;
      const glm::vec3 *normals = nullptr;
      glm::vec4 *tangents = nullptr;
      const glm::vec2 *uvs = nullptr;
    };

    SMikkTSpaceInterface iface = {
        .m_getNumFaces = [](const SMikkTSpaceContext *pContext) -> int {
          return ((const Context *)(pContext->m_pUserData))->num_faces;
        },

        .m_getNumVerticesOfFace = [](const SMikkTSpaceContext *,
                                     const int) -> int { return 3; },

        .m_getPosition =
            [](const SMikkTSpaceContext *pContext, float fvPosOut[],
               const int iFace, const int iVert) {
              glm::vec3 position = ((const Context *)(pContext->m_pUserData))
                                       ->positions[iFace * 3 + iVert];
              fvPosOut[0] = position.x;
              fvPosOut[1] = position.y;
              fvPosOut[2] = position.z;
            },

        .m_getNormal =
            [](const SMikkTSpaceContext *pContext, float fvNormOut[],
               const int iFace, const int iVert) {
              glm::vec3 normal = ((const Context *)(pContext->m_pUserData))
                                     ->normals[iFace * 3 + iVert];
              fvNormOut[0] = normal.x;
              fvNormOut[1] = normal.y;
              fvNormOut[2] = normal.z;
            },

        .m_getTexCoord =
            [](const SMikkTSpaceContext *pContext, float fvTexcOut[],
               const int iFace, const int iVert) {
              glm::vec2 tex_coord = ((const Context *)(pContext->m_pUserData))
                                        ->uvs[iFace * 3 + iVert];
              fvTexcOut[0] = tex_coord.x;
              fvTexcOut[1] = tex_coord.y;
            },

        .m_setTSpaceBasic =
            [](const SMikkTSpaceContext *pContext, const float fvTangent[],
               const float fSign, const int iFace, const int iVert) {
              glm::vec4 &tangent = ((const Context *)(pContext->m_pUserData))
                                       ->tangents[iFace * 3 + iVert];
              tangent.x = fvTangent[0];
              tangent.y = fvTangent[1];
              tangent.z = fvTangent[2];
              tangent.w = -fSign;
            },
    };

    Context user_data = {
        .num_faces = num_vertices / 3,
        .positions = positions.data(),
        .normals = normals.data(),
        .tangents = tangents.data(),
        .uvs = uvs.data(),
    };

    SMikkTSpaceContext ctx = {
        .m_pInterface = &iface,
        .m_pUserData = &user_data,
    };

    genTangSpaceDefault(&ctx);

    generate_index_buffer({});
  }

  // Optimize each LOD separately

  for (const glsl::MeshLOD &lod : mesh.lods) {
    meshopt_optimizeVertexCache(&indices[lod.base_index],
                                &indices[lod.base_index], lod.num_indices,
                                num_vertices);
  }

  // Optimize all LODs

  {
    remap.resize(num_vertices);
    num_vertices = meshopt_optimizeVertexFetchRemap(
        remap.data(), indices.data(), num_indices, num_vertices);
    meshopt_remapIndexBuffer(indices.data(), indices.data(), num_indices,
                             remap.data());
    remap_streams();
  }

  // Encode vertex attributes

  Vector<glsl::Position> enc_positions;
  {
    glsl::BoundingBox bb = {
        .min = glm::vec3(std::numeric_limits<float>::infinity()),
        .max = -glm::vec3(std::numeric_limits<float>::infinity()),
    };
    for (const glm::vec3 &position : positions) {
      mesh.position_encode_bounding_box =
          glm::max(mesh.position_encode_bounding_box, glm::abs(position));
      bb.min = glm::min(bb.min, position);
      bb.max = glm::max(bb.max, position);
    }
    mesh.position_encode_bounding_box =
        glm::exp2(glm::ceil(glm::log2(mesh.position_encode_bounding_box)));
    mesh.bounding_box =
        glsl::encode_bounding_box(bb, mesh.position_encode_bounding_box);

    enc_positions = positions | map([&](const glm::vec3 &position) {
                      return glsl::encode_position(
                          position, mesh.position_encode_bounding_box);
                    });
  }

  Vector<glsl::Normal> enc_normals;
  Vector<glsl::Tangent> enc_tangents;
  {
    glm::mat3 encode_transform_matrix =
        glsl::make_encode_position_matrix(mesh.position_encode_bounding_box);
    glm::mat3 encode_normal_matrix =
        glm::inverse(glm::transpose(encode_transform_matrix));

    enc_normals = normals | map([&](const glm::vec3 &normal) {
                    return glsl::encode_normal(
                        glm::normalize(encode_normal_matrix * normal));
                  });

    if (not tangents.empty()) {
      // Orthonormalize tangent space
      for (usize i = 0; i < num_vertices; ++i) {
        const glm::vec3 &normal = normals[i];
        glm::vec4 &tangent = tangents[i];
        glm::vec3 tangent3d(tangent);
        float sign = tangent.w;
        float proj = glm::dot(normal, tangent3d);
        tangent3d = glm::normalize(tangent3d - proj * normal);
        tangent = glm::vec4(tangent3d, sign);
      };
      enc_tangents =
          zip(tangents, enc_normals) | map([&](const auto &tangent_and_normal) {
            glm::vec4 tangent = std::get<0>(tangent_and_normal);
            tangent = glm::vec4(
                glm::normalize(encode_transform_matrix * glm::vec3(tangent)),
                tangent.w);
            glm::vec3 normal =
                glsl::decode_normal(std::get<1>(tangent_and_normal));
            // Encoding and then decoding the normal can change how the
            // tangent basis is selected due to rounding errors. Since
            // shaders use the decoded normal to decode the tangent, use
            // it for encoding as well
            return glsl::encode_tangent(tangent, normal);
          });
    }
  }

  Vector<glsl::UV> enc_uvs;
  if (not uvs.empty()) {
    for (glm::vec2 uv : uvs) {
      mesh.uv_bounding_square.min = glm::min(mesh.uv_bounding_square.min, uv);
      mesh.uv_bounding_square.max = glm::max(mesh.uv_bounding_square.max, uv);
    }

    // Round off the minimum and the maximum of the bounding square to the next
    // power of 2 if they are not equal to 0
    {
      // Select a relatively big default square size to avoid log2 NaN
      glm::vec2 p = glm::log2(glm::max(
          glm::max(-mesh.uv_bounding_square.min, mesh.uv_bounding_square.max),
          1.0f));
      glm::vec2 bs = glm::exp2(glm::ceil(p));
      mesh.uv_bounding_square.min =
          glm::mix(glm::vec2(0.0f), -bs,
                   glm::notEqual(mesh.uv_bounding_square.min, glm::vec2(0.0f)));
      mesh.uv_bounding_square.max =
          glm::mix(glm::vec2(0.0f), bs,
                   glm::notEqual(mesh.uv_bounding_square.max, glm::vec2(0.0f)));
    }

    enc_uvs = uvs | map([&](glm::vec2 uv) {
                return glsl::encode_uv(uv, mesh.uv_bounding_square);
              });
  }

  Vector<glsl::Color> enc_colors;
  if (not colors.empty()) {
    enc_colors = colors | map(glsl::encode_color);
  }

  // Find or allocate vertex pool

  auto &vertex_pool_list = m_vertex_pool_lists[usize(attributes.get())];
  if (vertex_pool_list.empty()) {
    vertex_pool_list.emplace_back(create_vertex_pool(attributes));
  } else {
    const VertexPool &pool = vertex_pool_list.back();
    if (pool.num_free_indices < num_indices or
        pool.num_free_vertices < num_vertices) {
      vertex_pool_list.emplace_back(create_vertex_pool(attributes));
    }
  }
  ren_assert(not vertex_pool_list.empty());
  mesh.pool = vertex_pool_list.size() - 1;
  VertexPool &vertex_pool = vertex_pool_list[mesh.pool];

  mesh.base_vertex =
      glsl::NUM_VERTEX_POOL_VERTICES - vertex_pool.num_free_vertices;
  mesh.base_index =
      glsl::NUM_VERTEX_POOL_INDICES - vertex_pool.num_free_indices;
  mesh.num_indices = num_indices;
  for (glsl::MeshLOD &lod : mesh.lods) {
    lod.base_index += mesh.base_index;
  }

  vertex_pool.num_free_vertices -= num_vertices;
  vertex_pool.num_free_indices -= num_indices;

  // Upload vertices

  {
    auto positions_dst =
        g_renderer->get_buffer_view(vertex_pool.positions)
            .slice<glsl::Position>(mesh.base_vertex, num_vertices);

    m_resource_uploader.stage_buffer(m_frame_arena, Span(enc_positions),
                                     positions_dst);
  }

  {
    auto normals_dst = g_renderer->get_buffer_view(vertex_pool.normals)
                           .slice<glsl::Normal>(mesh.base_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, Span(enc_normals),
                                     normals_dst);
  }

  if (not enc_tangents.empty()) {
    auto tangents_dst =
        g_renderer->get_buffer_view(vertex_pool.tangents)
            .slice<glsl::Tangent>(mesh.base_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, Span(enc_tangents),
                                     tangents_dst);
  }

  if (not enc_uvs.empty()) {
    auto uvs_dst = g_renderer->get_buffer_view(vertex_pool.uvs)
                       .slice<glsl::UV>(mesh.base_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, Span(enc_uvs), uvs_dst);
  }

  if (not enc_colors.empty()) {
    auto colors_dst = g_renderer->get_buffer_view(vertex_pool.colors)
                          .slice<glsl::Color>(mesh.base_vertex, num_vertices);
    m_resource_uploader.stage_buffer(m_frame_arena, Span(enc_colors),
                                     colors_dst);
  }

  {
    auto indices_dst = g_renderer->get_buffer_view(vertex_pool.indices)
                           .slice<u32>(mesh.base_index, mesh.num_indices);
    m_resource_uploader.stage_buffer(m_frame_arena, Span(indices), indices_dst);
  }

  auto key = std::bit_cast<MeshId>(u32(m_meshes.size()));
  m_meshes.push_back(mesh);

  return key;
}

auto SceneImpl::get_or_create_sampler(const SamplerDesc &sampler)
    -> Handle<Sampler> {
  AutoHandle<Sampler> &handle = m_samplers[sampler];
  if (!handle) {
    handle = g_renderer->create_sampler({
        .mag_filter = getVkFilter(sampler.mag_filter),
        .min_filter = getVkFilter(sampler.min_filter),
        .mipmap_mode = getVkSamplerMipmapMode(sampler.mipmap_filter),
        .address_mode_u = getVkSamplerAddressMode(sampler.wrap_u),
        .address_mode_v = getVkSamplerAddressMode(sampler.wrap_v),
        .anisotropy = 16.0f,
    });
  }
  return handle;
}

auto SceneImpl::get_or_create_texture(ImageId image,
                                      const SamplerDesc &sampler_desc)
    -> SampledTextureId {
  auto view = g_renderer->get_texture_view(m_images[image]);
  auto sampler = get_or_create_sampler(sampler_desc);
  return m_texture_allocator->allocate_sampled_texture(view, sampler);
}

auto SceneImpl::create_image(const ImageDesc &desc) -> ImageId {
  auto format = getVkFormat(desc.format);
  auto texture = g_renderer->create_texture({
      .type = VK_IMAGE_TYPE_2D,
      .format = format,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .width = desc.width,
      .height = desc.height,
      .num_mip_levels = get_mip_level_count(desc.width, desc.height),
  });
  usize size = desc.width * desc.height * get_format_size(format);
  m_resource_uploader.stage_texture(
      m_frame_arena, Span((const std::byte *)desc.data, size), texture);
  auto image = std::bit_cast<ImageId>(u32(m_images.size()));
  m_images.push_back(std::move(texture));
  return image;
}

void SceneImpl::create_materials(Span<const MaterialDesc> descs,
                                 MaterialId *out) {
  for (const auto &desc : descs) {
    glsl::Material material = {
        .base_color = desc.base_color_factor,
        .base_color_texture = [&]() -> u32 {
          if (desc.base_color_texture.image) {
            return get_or_create_texture(desc.base_color_texture.image,
                                         desc.base_color_texture.sampler);
          }
          return 0;
        }(),
        .metallic = desc.metallic_factor,
        .roughness = desc.roughness_factor,
        .metallic_roughness_texture = [&]() -> u32 {
          if (desc.metallic_roughness_texture.image) {
            return get_or_create_texture(
                desc.metallic_roughness_texture.image,
                desc.metallic_roughness_texture.sampler);
          }
          return 0;
        }(),
        .normal_texture = [&]() -> u32 {
          if (desc.normal_texture.image) {
            return get_or_create_texture(desc.normal_texture.image,
                                         desc.normal_texture.sampler);
          }
          return 0;
        }(),
        .normal_scale = desc.normal_texture.scale,
    };

    auto index = std::bit_cast<MaterialId>(u32(m_materials.size()));
    m_materials.push_back(material);

    *out = index;
    ++out;
  }
}

void SceneImpl::set_camera(const CameraDesc &desc) noexcept {
  m_camera = Camera{
      .position = desc.position,
      .forward = glm::normalize(desc.forward),
      .up = glm::normalize(desc.up),
      .projection = desc.projection,
  };

  m_pp_opts.exposure = {
      .mode = [&]() -> ExposureOptions::Mode {
        switch (desc.exposure_mode) {
        default:
          unreachable("Unknown exposure mode");
        case ExposureMode::Camera:
          return ExposureOptions::Camera{
              .aperture = desc.aperture,
              .shutter_time = desc.shutter_time,
              .iso = desc.iso,
              .exposure_compensation = desc.exposure_compensation,
          };
        case ExposureMode::Automatic:
          return ExposureOptions::Automatic{
              .exposure_compensation = desc.exposure_compensation,
          };
        }
      }(),
  };

  m_viewport_width = desc.width;
  m_viewport_height = desc.height;
}

void SceneImpl::set_tone_mapping(const ToneMappingDesc &oper) noexcept {
  m_pp_opts.tone_mapping = {
      .oper = oper,
  };
};

void SceneImpl::create_mesh_instances(Span<const MeshInstanceDesc> descs,
                                      Span<const glm::mat4x3> transforms,
                                      MeshInstanceId *out) {
  if (transforms.empty()) {
    for (const MeshInstanceDesc &desc : descs) {
      ren_assert(desc.mesh);
      ren_assert(desc.material);
      const Mesh &mesh = m_meshes[desc.mesh];
      Handle<MeshInstance> mesh_instance = m_mesh_instances.insert({
          .mesh = desc.mesh,
          .material = desc.material,
          .matrix = glsl::make_decode_position_matrix(
              mesh.position_encode_bounding_box),
      });
      *out = std::bit_cast<MeshInstanceId>(mesh_instance);
      ++out;
    }
  } else {
    ren_assert(descs.size() == transforms.size());
    for (const auto &[desc, transform] : zip(descs, transforms)) {
      ren_assert(desc.mesh);
      ren_assert(desc.material);
      const Mesh &mesh = m_meshes[desc.mesh];
      Handle<MeshInstance> mesh_instance = m_mesh_instances.insert({
          .mesh = desc.mesh,
          .material = desc.material,
          .matrix = transform * glsl::make_decode_position_matrix(
                                    mesh.position_encode_bounding_box),
      });
      *out = std::bit_cast<MeshInstanceId>(mesh_instance);
      ++out;
    }
  }
}

void SceneImpl::destroy_mesh_instances(
    Span<const MeshInstanceId> mesh_instances) noexcept {
  for (MeshInstanceId mesh_instance : mesh_instances) {
    m_mesh_instances.erase(std::bit_cast<Handle<MeshInstance>>(mesh_instance));
  }
}

void SceneImpl::set_mesh_instance_transforms(
    Span<const MeshInstanceId> mesh_instances,
    Span<const glm::mat4x3> matrices) noexcept {
  ren_assert(mesh_instances.size() == matrices.size());
  for (const auto &[handle, matrix] : zip(mesh_instances, matrices)) {
    MeshInstance &mesh_instance =
        m_mesh_instances[std::bit_cast<Handle<MeshInstance>>(handle)];
    const Mesh &mesh = m_meshes[mesh_instance.mesh];
    mesh_instance.matrix = matrix * glsl::make_decode_position_matrix(
                                        mesh.position_encode_bounding_box);
  }
}

auto SceneImpl::create_directional_light(const DirectionalLightDesc &desc)
    -> DirectionalLightId {
  auto light = m_dir_lights.insert(glsl::DirLight{
      .color = desc.color,
      .illuminance = desc.illuminance,
      .origin = desc.origin,
  });
  return std::bit_cast<DirectionalLightId>(light);
};

void SceneImpl::destroy_directional_light(DirectionalLightId light) noexcept {
  m_dir_lights.erase(std::bit_cast<Handle<glsl::DirLight>>(light));
}

void SceneImpl::update_directional_light(
    DirectionalLightId light, const DirectionalLightDesc &desc) noexcept {
  m_dir_lights[std::bit_cast<Handle<glsl::DirLight>>(light)] = {
      .color = desc.color,
      .illuminance = desc.illuminance,
      .origin = desc.origin,
  };
};

void SceneImpl::draw() {
  m_resource_uploader.upload(m_cmd_allocator);

  {
    ranges::fill(m_batch_max_counts, 0);
    for (const MeshInstance &mesh_instance : m_mesh_instances.values()) {
      const Mesh &mesh = m_meshes[mesh_instance.mesh];
      uint batch_id = glsl::get_batch_id(
          static_cast<uint32_t>(mesh.attributes.get()), mesh.pool);
      m_batch_max_counts[batch_id]++;
    }
    m_batch_offsets.assign(m_batch_max_counts |
                           ranges::views::exclusive_scan(0));
  }

  update_rg_passes(
      *m_render_graph, m_cmd_allocator,
      PassesConfig {
#if REN_IMGUI
        .imgui_context = m_imgui_context,
#endif
        .pipelines = &m_pipelines,
        .viewport = {m_viewport_width, m_viewport_height},
        .pp_opts = &m_pp_opts, .early_z = m_early_z,
      },
      PassesData{
          .batch_offsets = m_batch_offsets,
          .batch_max_counts = m_batch_max_counts,
          .vertex_pool_lists = m_vertex_pool_lists,
          .meshes = m_meshes,
          .materials = m_materials,
          .mesh_instances = m_mesh_instances.values(),
          .directional_lights = m_dir_lights.values(),
          .viewport = {m_viewport_width, m_viewport_height},
          .camera = &m_camera,
          .pp_opts = &m_pp_opts,
          .lod_triangle_pixels = m_lod_triangle_pixels,
          .lod_bias = m_lod_bias,
          .instance_frustum_culling = m_instance_frustum_culling,
          .lod_selection = m_lod_selection,
      });

  m_render_graph->execute(m_cmd_allocator);

  m_frame_arena.clear();
}

#if REN_IMGUI
void SceneImpl::draw_imgui() {
  ren_ImGuiScope(m_imgui_context);
  if (ImGui::GetCurrentContext()) {
    if (ImGui::Begin("Scene renderer settings")) {
      ImGui::SeparatorText("Instance culling");
      {
        bool frustum = m_instance_frustum_culling;
        ImGui::Checkbox("Frustum culling", &frustum);
        m_instance_frustum_culling = frustum;
      }

      ImGui::SeparatorText("Level of detail");
      {
        ImGui::SliderInt("LOD bias", &m_lod_bias, -(glsl::MAX_NUM_LODS - 1),
                         glsl::MAX_NUM_LODS - 1, "%d");

        bool selection = m_lod_selection;
        ImGui::Checkbox("LOD selection", &selection);
        m_lod_selection = selection;

        ImGui::BeginDisabled(!m_lod_selection);
        ImGui::SliderFloat("LOD pixels per triangle", &m_lod_triangle_pixels,
                           1.0f, 64.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        ImGui::EndDisabled();
      }

      ImGui::SeparatorText("Opaque pass");
      {
        bool early_z = m_early_z;
        ImGui::Checkbox("Early Z", &early_z);
        m_early_z = early_z;
      }

      ImGui::End();
    }
  }
}

void SceneImpl::set_imgui_context(ImGuiContext *context) noexcept {
  m_imgui_context = context;
  if (!context) {
    return;
  }
  ren_ImGuiScope(m_imgui_context);
  ImGuiIO &io = ImGui::GetIO();
  io.BackendRendererName = "imgui_impl_ren";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  u8 *data;
  i32 width, height;
  io.Fonts->GetTexDataAsRGBA32(&data, &width, &height);
  ren::ImageId image = create_image({
      .width = u32(width),
      .height = u32(height),
      .format = Format::RGBA8_UNORM,
      .data = data,
  });
  SamplerDesc desc = {
      .mag_filter = Filter::Linear,
      .min_filter = Filter::Linear,
      .mipmap_filter = Filter::Linear,
      .wrap_u = WrappingMode::Repeat,
      .wrap_v = WrappingMode::Repeat,
  };
  SampledTextureId texture = get_or_create_texture(image, desc);
  // NOTE: texture from old context is leaked. Don't really care since context
  // will probably be set only once
  io.Fonts->SetTexID((ImTextureID)(uintptr_t)texture);
}
#endif

} // namespace ren
