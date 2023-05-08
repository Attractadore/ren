#include "Descriptors.hpp"
#include "Device.hpp"
#include "Errors.hpp"

namespace ren {

DescriptorSetWriter::DescriptorSetWriter(Device &device, VkDescriptorSet set,
                                         DescriptorSetLayoutRef layout)
    : m_device(&device), m_set(set), m_layout(std::move(layout)) {}

auto DescriptorSetWriter::add_buffer(unsigned slot, BufferView buffer,
                                     unsigned offset) -> DescriptorSetWriter & {
  auto index = m_buffers.size();
  m_buffers.push_back(buffer.get_descriptor());
  m_data.push_back(VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_set,
      .dstBinding = slot,
      .dstArrayElement = offset,
      .descriptorCount = 1,
      .descriptorType = m_layout.desc->bindings[slot].type,
      .pBufferInfo = (VkDescriptorBufferInfo *)(index + 1),
  });
  return *this;
}

auto DescriptorSetWriter::add_texture_and_sampler(unsigned slot,
                                                  VkImageView view,
                                                  VkSampler sampler,
                                                  unsigned offset)
    -> DescriptorSetWriter & {
  auto type = m_layout.desc->bindings[slot].type;
  auto layout = [&] {
    switch (type) {
    default:
      unreachable("Invalid image/sampler descriptor type {}", int(type));
    case VK_DESCRIPTOR_TYPE_SAMPLER:
      return VK_IMAGE_LAYOUT_UNDEFINED;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      return VK_IMAGE_LAYOUT_GENERAL;
    }
  }();
  auto index = m_images.size();
  m_images.push_back(VkDescriptorImageInfo{
      .sampler = sampler,
      .imageView = view,
      .imageLayout = layout,
  });
  m_data.push_back(VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_set,
      .dstBinding = slot,
      .dstArrayElement = offset,
      .descriptorCount = 1,
      .descriptorType = type,
      .pImageInfo = (VkDescriptorImageInfo *)(index + 1),
  });
  return *this;
}

auto DescriptorSetWriter::add_texture(unsigned slot, const TextureView &view,
                                      unsigned offset)
    -> DescriptorSetWriter & {
  return add_texture_and_sampler(slot, m_device->getVkImageView(view), nullptr,
                                 offset);
}

auto DescriptorSetWriter::add_sampler(unsigned slot, const SamplerRef &sampler,
                                      unsigned offset)
    -> DescriptorSetWriter & {
  return add_texture_and_sampler(slot, nullptr, sampler.handle, offset);
}

auto DescriptorSetWriter::add_texture_and_sampler(unsigned slot,
                                                  const TextureView &view,
                                                  const SamplerRef &sampler,
                                                  unsigned offset)
    -> DescriptorSetWriter & {
  return add_texture_and_sampler(slot, m_device->getVkImageView(view),
                                 sampler.handle, offset);
}

auto DescriptorSetWriter::write() -> VkDescriptorSet {
  for (auto &write : m_data) {
    if (write.pBufferInfo) {
      write.pBufferInfo = &m_buffers[(size_t)write.pBufferInfo - 1];
    } else {
      assert(write.pImageInfo);
      write.pImageInfo = &m_images[(size_t)write.pImageInfo - 1];
    }
  }
  m_device->write_descriptor_sets(m_data);
  return m_set;
}

} // namespace ren
