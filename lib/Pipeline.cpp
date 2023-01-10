#include "Device.hpp"

namespace ren {
auto GraphicsPipelineBuilder::build() -> GraphicsPipeline {
  return m_device->create_graphics_pipeline(std::exchange(m_config, {}));
}
} // namespace ren
