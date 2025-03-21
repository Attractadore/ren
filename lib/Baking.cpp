#include "Baking.hpp"
#include "ren/baking/baking.hpp"

auto ren::create_baker(IRenderer *irenderer) -> expected<IBaker *> {
  Renderer *renderer = static_cast<Renderer *>(irenderer);
  auto baker = std::make_unique<IBaker>(IBaker{
      .renderer = renderer,
      .arena = ResourceArena(*renderer),
      .bake_arena = ResourceArena(*renderer),
      .rg = RgPersistent(*renderer),
  });
  baker->rg.set_async_compute_enabled(false);
  ren_try(baker->cmd_pool, baker->arena.create_command_pool({
                               .name = "Baker command pool",
                               .queue_family = rhi::QueueFamily::Graphics,
                           }));
  ren_try_to(baker->descriptor_allocator.init(baker->arena));
  ren_try_to(
      baker->bake_descriptor_allocator.init(baker->descriptor_allocator));
  baker->allocator.init(*baker->renderer, baker->arena, 64 * MiB);
  baker->upload_allocator.init(*baker->renderer, baker->arena, 64 * MiB);
  return baker.release();
}

void ren::destroy_baker(IBaker *baker) { delete baker; }

void ren::reset_baker(IBaker &baker) {
  baker.bake_arena.clear();
  baker.rg.reset();
  baker.bake_descriptor_allocator.reset();
  baker.allocator.reset();
  baker.upload_allocator.reset();
}
