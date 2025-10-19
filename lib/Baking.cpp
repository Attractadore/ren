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

auto create_baker(NotNull<Arena *> arena, Renderer *renderer)
    -> expected<Baker *> {
  auto baker = new Baker{.renderer = renderer};
  baker->arena = arena;
  baker->frame_arena = Arena::init();
  baker->rcs_arena = ResourceArena::init(arena, renderer);
  baker->frame_rcs_arena = ResourceArena::init(&baker->frame_arena, renderer);
  baker->rg = RgPersistent::init(&baker->frame_arena, renderer);
  ren_try(baker->cmd_pool, baker->rcs_arena.create_command_pool({
                               .name = "Baker command pool",
                               .queue_family = rhi::QueueFamily::Graphics,
                           }));
  baker->descriptor_allocator = DescriptorAllocator::init(arena);
  baker->frame_descriptor_allocator =
      DescriptorAllocatorScope::init(&baker->descriptor_allocator);
  baker->allocator =
      DeviceBumpAllocator::init(*baker->renderer, baker->rcs_arena, 64 * MiB);
  baker->upload_allocator =
      UploadBumpAllocator::init(*baker->renderer, baker->rcs_arena, 256 * MiB);
  return baker;
}

void destroy_baker(Baker *baker) {
  baker->renderer->wait_idle();
  baker->rcs_arena.clear();
  baker->frame_rcs_arena.clear();
  baker->rg.destroy();
  baker->frame_arena.destroy();
  delete baker;
}

void reset_baker(Baker *baker) {
  baker->renderer->wait_idle();
  baker->rg.destroy();
  baker->frame_arena.clear();
  baker->frame_rcs_arena.clear();
  baker->rg = RgPersistent::init(&baker->frame_arena, baker->renderer);
  std::ignore = baker->renderer->reset_command_pool(baker->cmd_pool);
  baker->frame_descriptor_allocator.reset();
  baker->allocator.reset();
  baker->upload_allocator.reset();
}

} // namespace ren
