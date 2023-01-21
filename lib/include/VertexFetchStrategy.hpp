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

template <class Derived> class VertexFetcherMixin {

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
      -> hlsl::VertexData {
    return {.matrix_index = matrix_index};
  }

  auto get_vertex_push_constants(Device &device, const Mesh &mesh,
                                 unsigned matrix_index) const
      -> hlsl::VertexData {
    return pimpl()->get_vertex_push_constants_impl(device, mesh, matrix_index);
  }

  static auto get_pixel_push_constants(const Material &material)
      -> hlsl::FragmentData {
    return {.material_index = material.index};
  }

public:
  static void get_push_constants(CVector<PushConstantRange> auto &out) {
    out = {
        {.stages = VK_SHADER_STAGE_VERTEX_BIT,
         .offset = offsetof(hlsl::PushConstants, vertex),
         .size = sizeof(hlsl::PushConstants::vertex)},
        {.stages = VK_SHADER_STAGE_FRAGMENT_BIT,
         .offset = offsetof(hlsl::PushConstants, fragment),
         .size = sizeof(hlsl::PushConstants::fragment)},
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
    hlsl::PushConstants data = {
        .vertex =
            get_vertex_push_constants(cmd.get_device(), mesh, matrix_index),
    };
    cmd.set_graphics_push_constants(signature, VK_SHADER_STAGE_VERTEX_BIT,
                                    data.vertex,
                                    offsetof(decltype(data), vertex));
  }

  void set_pixel_push_constants(CommandBuffer &cmd, PipelineLayoutRef signature,

                                const Material &material) const {
    hlsl::PushConstants data = {
        .fragment = get_pixel_push_constants(material),
    };
    cmd.set_graphics_push_constants(signature, VK_SHADER_STAGE_FRAGMENT_BIT,
                                    data.fragment,
                                    offsetof(decltype(data), fragment));
  }
};

class VertexFetchPhysical : public VertexFetcherMixin<VertexFetchPhysical> {
  using Base = VertexFetcherMixin<VertexFetchPhysical>;
  friend Base;

  auto get_vertex_push_constants_impl(Device &device, const Mesh &mesh,
                                      unsigned matrix_index) const
      -> hlsl::VertexData {
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
using VertexFetchStrategy = VertexFetchStrategyImpl<VertexFetchPhysical>;
static_assert(CVertexFetchStrategy<VertexFetchStrategy>);
} // namespace ren
