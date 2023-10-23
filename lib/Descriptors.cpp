#include "Descriptors.hpp"
#include "Renderer.hpp"
#include "Support/Errors.hpp"

namespace ren {

auto allocate_descriptor_pool_and_set(Handle<DescriptorSetLayout> layout_handle)
    -> std::tuple<AutoHandle<DescriptorPool>, VkDescriptorSet> {
  const auto &layout = g_renderer->get_descriptor_set_layout(layout_handle);

  VkDescriptorSetLayoutCreateFlags flags = 0;
  if (layout.flags &
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT) {
    flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  }

  std::array<unsigned, DESCRIPTOR_TYPE_COUNT> pool_sizes = {};
  for (const auto &binding : layout.bindings) {
    pool_sizes[binding.type] += binding.count;
  }

  auto pool = g_renderer->create_descriptor_pool({
      .flags = flags,
      .set_count = 1,
      .pool_sizes = pool_sizes,
  });

  auto set = g_renderer->allocate_descriptor_set(pool, layout_handle);
  assert(set);

  return {std::move(pool), *set};
}

} // namespace ren
