#pragma once
#include "Device.hpp"
#include "ResourceUploader.hpp"
#include "Support/Span.hpp"
#include "hlsl/material.hpp"
#include "ren/ren.h"

#include <glm/gtc/type_ptr.hpp>

namespace ren {

class MaterialAllocator {
  Buffer m_buffer;
  // Sentinel value in the front so MaterialID = 0 can correspond to null
  // material
  Vector<hlsl::Material> m_materials = {{}};

public:
  unsigned allocate(Device &device, const RenMaterialDesc &desc,
                    ResourceUploader &uploader) {
    auto index = m_materials.size();
    m_materials.emplace_back() = {
        .base_color = glm::make_vec4(desc.base_color_factor),
        .metallic = desc.metallic_factor,
        .roughness = desc.roughness_factor,
    };

    unsigned cpu_size = std::span(m_materials).size_bytes();
    auto gpu_size = m_buffer.desc.size;
    if (cpu_size > gpu_size) {
      auto new_gpu_size = std::max(2 * gpu_size, cpu_size);
      m_buffer = device.create_buffer({
          .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
          .heap = BufferHeap::Device,
          .size = new_gpu_size,
      });
      uploader.stage_buffer(device, m_materials, m_buffer);
    } else {
      uploader.stage_buffer(device, asSpan(m_materials[index]), m_buffer,
                            index * sizeof(hlsl::Material));
    }

    return index;
  };

  const Buffer &get_buffer() const { return m_buffer; }
};

} // namespace ren
