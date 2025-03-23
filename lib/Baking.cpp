#include "Baking.hpp"
#include "ren/baking/baking.hpp"

namespace ren {

namespace {
auto init_baker_samplers(ResourceArena &arena) -> Result<BakerSamplers, Error> {
  BakerSamplers samplers;
  ren_try(samplers.mip_linear_wrap_clamp,
          arena.create_sampler({
              .mag_filter = rhi::Filter::Linear,
              .min_filter = rhi::Filter::Linear,
              .mipmap_mode = rhi::SamplerMipmapMode::Linear,
              .address_mode_u = rhi::SamplerAddressMode::Repeat,
              .address_mode_v = rhi::SamplerAddressMode::ClampToEdge,
              .anisotropy = 16.0f,
          }));
  return samplers;
};
} // namespace

} // namespace ren

auto ren::create_baker(IRenderer *irenderer) -> expected<IBaker *> {
  Renderer *renderer = static_cast<Renderer *>(irenderer);
  auto baker = std::make_unique<IBaker>(IBaker{
      .renderer = renderer,
      .session_arena = ResourceArena(*renderer),
      .arena = ResourceArena(*renderer),
      .rg = RgPersistent(*renderer),
  });
  baker->rg.set_async_compute_enabled(false);
  ren_try(baker->cmd_pool, baker->session_arena.create_command_pool({
                               .name = "Baker command pool",
                               .queue_family = rhi::QueueFamily::Graphics,
                           }));
  ren_try_to(baker->session_descriptor_allocator.init(baker->session_arena));
  ren_try_to(
      baker->descriptor_allocator.init(baker->session_descriptor_allocator));
  baker->allocator.init(*baker->renderer, baker->session_arena, 64 * MiB);
  baker->upload_allocator.init(*baker->renderer, baker->session_arena,
                               256 * MiB);
  ren_try(baker->samplers, init_baker_samplers(baker->session_arena));
  return baker.release();
}

void ren::destroy_baker(IBaker *baker) { delete baker; }

void ren::reset_baker(IBaker &baker) {
  baker.arena.clear();
  baker.renderer->reset_command_pool(baker.cmd_pool).value();
  baker.rg.reset();
  baker.descriptor_allocator.reset();
  baker.allocator.reset();
  baker.upload_allocator.reset();
}
