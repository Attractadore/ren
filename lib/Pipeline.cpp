#include "Device.hpp"

namespace ren {
auto GraphicsPipelineBuilder::build() -> Pipeline {
  return m_device->create_graphics_pipeline(m_desc);
}
} // namespace ren
