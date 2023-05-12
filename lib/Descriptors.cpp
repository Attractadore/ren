#include "Descriptors.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "ResourceArena.hpp"

namespace ren {

DescriptorSetWriter::DescriptorSetWriter(VkDescriptorSet set,
                                         const DescriptorSetLayout &layout)
    : m_set(set), m_layout(&layout) {}

auto DescriptorSetWriter::add_buffer(unsigned slot, const BufferView &view,
                                     unsigned offset) -> DescriptorSetWriter & {
  auto index = m_buffers.size();
  m_buffers.push_back(view.get_descriptor());
  m_data.push_back(VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_set,
      .dstBinding = slot,
      .dstArrayElement = offset,
      .descriptorCount = 1,
      .descriptorType = m_layout->bindings[slot].type,
      .pBufferInfo = (VkDescriptorBufferInfo *)(index + 1),
  });
  return *this;
}

auto DescriptorSetWriter::add_texture_and_sampler(unsigned slot,
                                                  VkImageView view,
                                                  VkSampler sampler,
                                                  unsigned offset)
    -> DescriptorSetWriter & {
  auto type = m_layout->bindings[slot].type;
  auto layout = [&] {
    switch (type) {
    default:
      unreachable("Invalid image/sampler descriptor type {}", int(type));
    case VK_DESCRIPTOR_TYPE_SAMPLER:
      assert(view == nullptr);
      return VK_IMAGE_LAYOUT_UNDEFINED;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      assert(sampler == nullptr);
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

auto DescriptorSetWriter::add_texture(unsigned slot, VkImageView view,
                                      unsigned offset)
    -> DescriptorSetWriter & {
  return add_texture_and_sampler(slot, view, nullptr, offset);
}

auto DescriptorSetWriter::add_sampler(unsigned slot, const Sampler &sampler,
                                      unsigned offset)
    -> DescriptorSetWriter & {
  return add_texture_and_sampler(slot, nullptr, sampler.handle, offset);
}

auto DescriptorSetWriter::add_texture_and_sampler(unsigned slot,
                                                  VkImageView view,
                                                  const Sampler &sampler,
                                                  unsigned offset)
    -> DescriptorSetWriter & {
  return add_texture_and_sampler(slot, view, sampler.handle, offset);
}

auto DescriptorSetWriter::write(Device &device) -> VkDescriptorSet {
  for (auto &write : m_data) {
    if (write.pBufferInfo) {
      write.pBufferInfo = &m_buffers[(size_t)write.pBufferInfo - 1];
    } else {
      assert(write.pImageInfo);
      write.pImageInfo = &m_images[(size_t)write.pImageInfo - 1];
    }
  }
  device.write_descriptor_sets(m_data);
  return m_set;
}

auto allocate_descriptor_pool_and_set(Device &device, ResourceArena &arena,
                                      Handle<DescriptorSetLayout> layout_handle)
    -> std::tuple<Handle<DescriptorPool>, VkDescriptorSet> {
  const auto &layout = device.get_descriptor_set_layout(layout_handle);

  VkDescriptorSetLayoutCreateFlags flags = 0;
  if (layout.flags &
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT) {
    flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  }

  std::array<unsigned, DESCRIPTOR_TYPE_COUNT> pool_sizes = {};
  for (const auto &binding : layout.bindings) {
    pool_sizes[binding.type] += binding.count;
  }

  auto pool = arena.create_descriptor_pool({
      .flags = flags,
      .set_count = 1,
      .pool_sizes = pool_sizes,
  });

  auto set = device.allocate_descriptor_set(pool, layout_handle);
  assert(set);

  return {pool, *set};
}

} // namespace ren
