#include "ren/ren.hpp"
#include "Lippincott.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "Support/Errors.hpp"
#include "Swapchain.hpp"
#include "ren/ren-vk.hpp"

#include <range/v3/algorithm.hpp>
#include <range/v3/view.hpp>

namespace ren {

Renderer *g_renderer = nullptr;

namespace {
std::unique_ptr<Renderer> g_renderer_holder;
SmallVector<SceneImpl *, 1> g_scenes;
} // namespace

auto init(const InitInfo &init_info) -> expected<void> {
  return lippincott([&] {
    g_renderer_holder = std::make_unique<Renderer>(
        init_info.instance_extensions, init_info.adapter);
    g_renderer = g_renderer_holder.get();
  });
}

void quit() {
  ren_assert_msg(g_scenes.empty(), "All scenes must have been destroyed");
  g_renderer_holder.reset();
  g_renderer = nullptr;
}

auto draw() -> expected<void> {
  return lippincott([] {
    for (SceneImpl *scene : g_scenes) {
      scene->draw();
    }
    g_renderer->next_frame();
    for (SceneImpl *scene : g_scenes) {
      scene->next_frame();
    }
  });
}

GlobalScene::GlobalScene(SwapchainImpl &swapchain) : SceneImpl(swapchain) {
  g_scenes.push_back(this);
}

GlobalScene::~GlobalScene() { g_scenes.unstable_erase(this); }

} // namespace ren
