#include "app-base.hpp"
#include "app-vk.hpp"

void AppBase::select_renderer() {
  m_renderer = std::make_unique<VulkanRenderer>();
}
