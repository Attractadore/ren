#include "ren/ren.hpp"
#include "Renderer.hpp"

namespace ren {

auto create_renderer(const RendererCreateInfo &desc)
    -> expected<std::unique_ptr<IRenderer>> {
  auto renderer = std::make_unique<Renderer>();
  return renderer->init(desc.adapter).transform([&] {
    return std::move(renderer);
  });
}

} // namespace ren
