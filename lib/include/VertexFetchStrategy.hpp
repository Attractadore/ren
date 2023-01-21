#pragma once
#include "Device.hpp"
#include "Formats.inl"
#include "Material.hpp"
#include "Mesh.hpp"
#include "Pipeline.hpp"
#include "ResourceUploader.hpp"
#include "Support/HashMap.hpp"
#include "Support/Span.hpp"
#include "Support/Views.hpp"
#include "hlsl/encode.h"
#include "hlsl/interface.hpp"

#include <cassert>

namespace ren {

template <typename T>
concept CVertexFetchStrategy =
    requires(T vf, const T cvf) {
      { cvf.get_type() } -> std::same_as<hlsl::VertexFetch>;

      {
        requires(Vector<PushConstantRange> & out) {
          { cvf.get_push_constants(out) } -> std::same_as<void>;
        }
      };

      { cvf.get_buffer_usage_flags() } -> std::same_as<VkBufferUsageFlags>;

      {
        requires(MeshAttribute attribute) {
          { cvf.get_mesh_attribute_size() } -> std::integral;
        }
      };
      {
        requires(ResourceUploader & uploader, MeshAttribute attribute,
                 std::span<const std::byte> data, BufferRef buffer,
                 unsigned offset) {
          {
            cvf.upload_mesh_attribute(uploader, attribute, data, buffer, offset)
            } -> std::integral;
        }
      };

      {
        requires(CommandBuffer & cmd, PipelineLayoutRef signature,
                 const Mesh &mesh, unsigned matrix_index) {
          {
            cvf.set_vertex_push_constants(cmd, signature, mesh, matrix_index)
            } -> std::same_as<void>;
        }
      };

      {
        requires(CommandBuffer & cmd, PipelineLayoutRef signature,
                 const Material &material) {
          {
            cvf.set_pixel_push_constants(cmd, signature, material)
            } -> std::same_as<void>;
        }
      };
    };

template <hlsl::VertexFetch VF, class Derived> class VertexFetcherMixin {
  static constexpr auto type = VF;

protected:
  const Derived *pimpl() const { return static_cast<const Derived *>(this); }
  Derived *pimpl() { return static_cast<Derived *>(this); }

  template <typename T, std::invocable<T> Encoder = std::identity>
  static auto upload_data(ResourceUploader &uploader, std::span<const T> data,
                          BufferRef buffer, unsigned offset,
                          Encoder encoder = Encoder{}) -> unsigned {
    auto encoded_data = data | map(std::move(encoder));
    uploader.stage_data(encoded_data, buffer, offset);
    return size_bytes(encoded_data);
  }

  static auto upload_vertex_positions(ResourceUploader &uploader,
                                      std::span<const glm::vec3> positions,
                                      BufferRef buffer, unsigned offset)
      -> unsigned {
    return upload_data(uploader, positions, buffer, offset);
  }

  static auto upload_vertex_colors(ResourceUploader &uploader,
                                   std::span<const glm::vec3> colors,
                                   BufferRef buffer, unsigned offset)
      -> unsigned {
    return upload_data(uploader, colors, buffer, offset);
  }

  static auto get_vertex_position_size() { return sizeof(glm::vec3); }

  static auto get_vertex_color_size() { return sizeof(glm::vec3); }

  auto get_vertex_push_constants_impl(Device &, const Mesh &mesh,
                                      unsigned matrix_index) const
      -> hlsl::VertexDataTemplate<VF> {
    return {.matrix_index = matrix_index};
  }

  auto get_vertex_push_constants(Device &device, const Mesh &mesh,
                                 unsigned matrix_index) const
      -> hlsl::VertexDataTemplate<VF> {
    return pimpl()->get_vertex_push_constants_impl(device, mesh, matrix_index);
  }

  static auto get_pixel_push_constants(const Material &material)
      -> hlsl::PixelData {
    return {.material_index = material.index};
  }

public:
  static constexpr auto get_type() -> hlsl::VertexFetch { return type; }

  static void get_push_constants(CVector<PushConstantRange> auto &out) {
    out = {
        {.stages = VK_SHADER_STAGE_VERTEX_BIT,
         .offset = offsetof(hlsl::PushConstantsTemplate<VF>, vertex),
         .size = sizeof(hlsl::PushConstantsTemplate<VF>::vertex)},
        {.stages = VK_SHADER_STAGE_FRAGMENT_BIT,
         .offset = offsetof(hlsl::PushConstantsTemplate<VF>, pixel),
         .size = sizeof(hlsl::PushConstantsTemplate<VF>::pixel)},
    };
  }

  auto upload_mesh_attribute(ResourceUploader &uploader,
                             MeshAttribute attribute,
                             std::span<const std::byte> data, BufferRef buffer,
                             unsigned offset) const -> unsigned {
    switch (attribute) {
    default:
      assert(!"Unknown mesh attribute");
    case MESH_ATTRIBUTE_POSITIONS: {
      auto positions = reinterpret_span<const glm::vec3>(data);
      return pimpl()->upload_vertex_positions(uploader, positions, buffer,
                                              offset);
    }
    case MESH_ATTRIBUTE_COLORS: {
      auto colors = reinterpret_span<const glm::vec3>(data);
      return pimpl()->upload_vertex_colors(uploader, colors, buffer, offset);
    }
    }
  }

  auto get_mesh_attribute_size(MeshAttribute attribute) const -> unsigned {
    switch (attribute) {
    default:
      assert(!"Unknown mesh attribute");
    case MESH_ATTRIBUTE_POSITIONS: {
      return pimpl()->get_vertex_position_size();
    }
    case MESH_ATTRIBUTE_COLORS: {
      return pimpl()->get_vertex_color_size();
    }
    }
  }

  void set_vertex_push_constants(CommandBuffer &cmd,
                                 PipelineLayoutRef signature, const Mesh &mesh,
                                 unsigned matrix_index) const {
    hlsl::PushConstantsTemplate<VF> data = {
        .vertex =
            get_vertex_push_constants(cmd.get_device(), mesh, matrix_index),
    };
    cmd.set_graphics_push_constants(signature, VK_SHADER_STAGE_VERTEX_BIT,
                                    data.vertex,
                                    offsetof(decltype(data), vertex));
  }

  void set_pixel_push_constants(CommandBuffer &cmd, PipelineLayoutRef signature,

                                const Material &material) const {
    hlsl::PushConstantsTemplate<VF> data = {
        .pixel = get_pixel_push_constants(material),
    };
    cmd.set_graphics_push_constants(signature, VK_SHADER_STAGE_FRAGMENT_BIT,
                                    data.pixel,
                                    offsetof(decltype(data), pixel));
  }
};

class VertexFetchPhysical
    : public VertexFetcherMixin<hlsl::VertexFetch::Physical,
                                VertexFetchPhysical> {
  using Base =
      VertexFetcherMixin<hlsl::VertexFetch::Physical, VertexFetchPhysical>;
  friend Base;

  auto get_vertex_push_constants_impl(Device &device, const Mesh &mesh,
                                      unsigned matrix_index) const
      -> hlsl::VertexDataTemplate<get_type()> {
    auto addr = device.get_buffer_device_address(mesh.vertex_allocation);
    auto positions_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_POSITIONS];
    auto colors_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_COLORS];
    return {.matrix_index = matrix_index,
            .positions = addr + positions_offset,
            .colors =
                (colors_offset != ATTRIBUTE_UNUSED) ? addr + colors_offset : 0};
  }

  static auto upload_vertex_colors(ResourceUploader &uploader,
                                   std::span<const glm::vec3> colors,
                                   BufferRef buffer, unsigned offset)
      -> unsigned {
    return upload_data(uploader, colors, buffer, offset, hlsl::encode_color);
  }

  static auto get_vertex_color_size() { return sizeof(hlsl::color_t); }

public:
  static auto get_buffer_usage_flags() -> VkBufferUsageFlags {
    return VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  }
};

class VertexFetchLogical : public VertexFetcherMixin<hlsl::VertexFetch::Logical,
                                                     VertexFetchLogical> {
  using Base =
      VertexFetcherMixin<hlsl::VertexFetch::Logical, VertexFetchLogical>;
  friend Base;

  static auto upload_vertex_colors(ResourceUploader &uploader,
                                   std::span<const glm::vec3> colors,
                                   BufferRef buffer, unsigned offset)
      -> unsigned {
    return upload_data(uploader, colors, buffer, offset, hlsl::encode_color);
  }

  static auto get_vertex_color_size() -> unsigned {
    return sizeof(hlsl::color_t);
  }

public:
  static auto get_buffer_usage_flags() -> VkBufferUsageFlags {
    return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }
};

class VertexFetchAttribute
    : public VertexFetcherMixin<hlsl::VertexFetch::Attribute,
                                VertexFetchAttribute> {
  std::array<std::string_view, MESH_ATTRIBUTE_COUNT> m_mesh_attribute_semantics;
  std::array<VkFormat, MESH_ATTRIBUTE_COUNT> m_mesh_attribute_formats;
  std::array<unsigned, MESH_ATTRIBUTE_COUNT> m_mesh_attribute_sizes;
  HashMap<std::string_view, MeshAttribute> m_semantic_mesh_attributes;

public:
  VertexFetchAttribute() {
    m_mesh_attribute_semantics[MESH_ATTRIBUTE_POSITIONS] = "POSITION";
    m_mesh_attribute_semantics[MESH_ATTRIBUTE_COLORS] = "ALBEDO";

    m_mesh_attribute_formats[MESH_ATTRIBUTE_POSITIONS] =
        VK_FORMAT_R32G32B32_SFLOAT;
    m_mesh_attribute_formats[MESH_ATTRIBUTE_COLORS] =
        VK_FORMAT_R32G32B32_SFLOAT;

    for (int i = 0; i < MESH_ATTRIBUTE_COUNT; ++i) {
      auto mesh_attribute = static_cast<MeshAttribute>(i);
      auto semantic = m_mesh_attribute_semantics[mesh_attribute];
      m_semantic_mesh_attributes[semantic] = mesh_attribute;
      auto format = m_mesh_attribute_formats[mesh_attribute];
      m_mesh_attribute_sizes[mesh_attribute] = get_format_size(format);
    }
  }

  static auto get_buffer_usage_flags() -> VkBufferUsageFlags {
    return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  }

  auto get_semantic_format(std::string_view semantic) const -> VkFormat {
    return m_mesh_attribute_formats[get_semantic_mesh_attribute(semantic)];
  };

  auto get_semantic_mesh_attribute(std::string_view semantic) const
      -> MeshAttribute {
    auto it = m_semantic_mesh_attributes.find(semantic);
    assert(it != m_semantic_mesh_attributes.end());
    return it->second;
  }

  auto get_mesh_attribute_size(MeshAttribute attribute) const -> unsigned {
    return m_mesh_attribute_sizes[attribute];
  }
};

template <CVertexFetchStrategy... Ts> class VertexFetchStrategyImpl {
  std::variant<Ts...> m_impl;

public:
  VertexFetchStrategyImpl(CVertexFetchStrategy auto s) : m_impl(std::move(s)) {}

  auto get_type() const {
    return std::visit([&](const auto &vf) { return vf.get_type(); }, m_impl);
  }

  auto get_push_constants(CVector<PushConstantRange> auto &out) const {
    return std::visit(
        [&](const auto &vf) { return vf.get_push_constants(out); }, m_impl);
  }

  auto get_buffer_usage_flags() const {
    return std::visit(
        [&](const auto &vf) { return vf.get_buffer_usage_flags(); }, m_impl);
  }

  auto get_vertex_position_size() const {
    return std::visit(
        [&](const auto &vf) { return vf.get_vertex_position_size(); }, m_impl);
  }

  auto upload_vertex_positions(ResourceUploader &uploader,
                               std::span<const glm::vec3> positions,
                               BufferRef buffer, unsigned offset) const {
    return std::visit(
        [&](const auto &vf) {
          return vf.upload_vertex_positions(uploader, positions, buffer,
                                            offset);
        },
        m_impl);
  }

  auto get_mesh_attribute_size(MeshAttribute attribute) const {
    return std::visit(
        [&](const auto &vf) { return vf.get_mesh_attribute_size(attribute); },
        m_impl);
  }

  auto upload_mesh_attribute(ResourceUploader &uploader,
                             MeshAttribute attribute,
                             std::span<const std::byte> data, BufferRef buffer,
                             unsigned offset) const {
    return std::visit(
        [&](const auto &vf) {
          return vf.upload_mesh_attribute(uploader, attribute, data, buffer,
                                          offset);
        },
        m_impl);
  }

  auto set_vertex_push_constants(CommandBuffer &cmd,
                                 PipelineLayoutRef signature, const Mesh &mesh,
                                 unsigned matrix_index) const {
    return std::visit(
        [&](const auto &vf) {
          return vf.set_vertex_push_constants(cmd, signature, mesh,
                                              matrix_index);
        },
        m_impl);
  }

  auto set_pixel_push_constants(CommandBuffer &cmd, PipelineLayoutRef signature,

                                const Material &material) const {
    return std::visit(
        [&](const auto &vf) {
          return vf.set_pixel_push_constants(cmd, signature, material);
        },
        m_impl);
  }

  template <typename T> const T *get_if() const {
    return std::get_if<T>(&m_impl);
  }
};
using VertexFetchStrategy =
    VertexFetchStrategyImpl<VertexFetchPhysical, VertexFetchLogical,
                            VertexFetchAttribute>;
static_assert(CVertexFetchStrategy<VertexFetchStrategy>);
} // namespace ren
