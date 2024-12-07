#include "CommandAllocator.hpp"
#include "Renderer.hpp"
#include "core/Errors.hpp"

namespace ren {

CommandAllocator::CommandAllocator(Renderer &renderer) {
  m_renderer = &renderer;
  VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = m_renderer->get_graphics_queue_family(),
  };
  throw_if_failed(vkCreateCommandPool(m_renderer->get_device(), &pool_info,
                                      nullptr, &m_pool),
                  "Vulkan: Failed to create command pool");
}

CommandAllocator::CommandAllocator(CommandAllocator &&other) noexcept
    : m_renderer(other.m_renderer),
      m_pool(std::exchange(other.m_pool, nullptr)),
      m_cmd_buffers(std::move(other.m_cmd_buffers)),
      m_allocated_count(std::exchange(other.m_allocated_count, 0)) {}

CommandAllocator &
CommandAllocator::operator=(CommandAllocator &&other) noexcept {
  destroy();
  m_renderer = other.m_renderer;
  m_pool = other.m_pool;
  other.m_pool = nullptr;
  m_cmd_buffers = std::move(other.m_cmd_buffers);
  m_allocated_count = other.m_allocated_count;
  other.m_allocated_count = 0;
  return *this;
}

void CommandAllocator::destroy() {
  if (m_pool) {
    m_renderer->wait_idle();
    if (not m_cmd_buffers.empty()) {
      vkFreeCommandBuffers(m_renderer->get_device(), m_pool,
                           m_cmd_buffers.size(), m_cmd_buffers.data());
    }
    vkDestroyCommandPool(m_renderer->get_device(), m_pool, nullptr);
  }
}

CommandAllocator::~CommandAllocator() { destroy(); }

VkCommandBuffer CommandAllocator::allocate() {
  [[unlikely]] if (m_allocated_count == m_cmd_buffers.size()) {
    auto old_capacity = m_cmd_buffers.size();
    auto new_capacity = std::max<size_t>(2 * old_capacity, 1);
    m_cmd_buffers.resize(new_capacity);
    uint32_t alloc_count = new_capacity - old_capacity;
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = alloc_count,
    };
    throw_if_failed(
        vkAllocateCommandBuffers(m_renderer->get_device(), &alloc_info,
                                 m_cmd_buffers.data() + old_capacity),
        "Vulkan: Failed to allocate command buffers");
  }
  return m_cmd_buffers[m_allocated_count++];
}

void CommandAllocator::reset() {
  throw_if_failed(vkResetCommandPool(m_renderer->get_device(), m_pool, 0),
                  "Vulkan: Failed to reset command pool");
  m_allocated_count = 0;
}

} // namespace ren
