#include "ren/ren.hpp"
#include "Lippincott.hpp"
#include "Renderer.hpp"

namespace ren {

auto create_renderer(const RendererCreateInfo &desc)
    -> expected<std::unique_ptr<IRenderer>> {
  return lippincott([&] {
    return std::make_unique<Renderer>(desc.vk_instance_extensions,
                                      desc.adapter);
  });
}

} // namespace ren
