#include "Baking.hpp"
#include "ren/baking/baking.hpp"

namespace ren {

#if 0
namespace {
auto init_baker_samplers(ResourceArena &arena) -> Result<BakerSamplers, Error> {
  BakerSamplers samplers;
  ren_try(samplers.wrap_u_clamp_v,
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

#endif

auto create_baker(Renderer *renderer) -> expected<Baker *> {
  auto baker = new Baker{
      .renderer = renderer,
      .session_arena = ResourceArena(*renderer),
      .arena = ResourceArena(*renderer),
      .rg = RgPersistent(*renderer),
  };
  baker->rg.set_async_compute_enabled(false);
  ren_try(baker->cmd_pool, baker->session_arena.create_command_pool({
                               .name = "Baker command pool",
                               .queue_family = rhi::QueueFamily::Graphics,
                           }));
  ren_try_to(
      baker->descriptor_allocator.init(baker->session_descriptor_allocator));
  baker->allocator.init(*baker->renderer, baker->session_arena, 64 * MiB);
  baker->upload_allocator.init(*baker->renderer, baker->session_arena,
                               256 * MiB);
  return baker;
}

void destroy_baker(Baker *baker) { delete baker; }

void reset_baker(Baker *baker) {
  baker->arena.clear();
  std::ignore = baker->renderer->reset_command_pool(baker->cmd_pool);
  baker->rg.reset();
  baker->descriptor_allocator.reset();
  baker->allocator.reset();
  baker->upload_allocator.reset();
}

} // namespace ren
