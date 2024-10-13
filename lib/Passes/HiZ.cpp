#include "HiZ.hpp"
#include "CommandRecorder.hpp"
#include "Scene.hpp"
#include "Swapchain.hpp"
#include "glsl/HiZSpdPass.h"

#include <bit>

namespace ren {

void setup_hi_z_pass(const PassCommonConfig &ccfg, const HiZPassConfig &cfg) {
  glm::uvec2 viewport = ccfg.swapchain->get_size();
  glm::uvec2 size = {std::bit_floor(viewport.x), std::bit_floor(viewport.y)};
  u32 num_mips =
      std::max(std::countr_zero(size.x), std::countr_zero(size.y)) + 1;
  ren_assert(size.x < glsl::HI_Z_SPD_MAX_SIZE and
             size.y < glsl::HI_Z_SPD_MAX_SIZE);

  if (!ccfg.rcs->hi_z) {
    ccfg.rcs->hi_z = ccfg.rgp->create_texture({
        .name = "hi-z",
        .format = VK_FORMAT_R32_SFLOAT,
        .width = size.x,
        .height = size.y,
        .num_mip_levels = num_mips,
    });
  }

  RgBufferId<glsl::uint> counter;
  {
    auto init_pass = ccfg.rgb->create_pass({.name = "hi-z-init"});

    counter = ccfg.rgb->create_buffer<glsl::uint>(RgBufferCreateInfo{
        .heap = BufferHeap::Static,
        .count = 1,
    });

    RgBufferToken<glsl::uint> rcs;
    std::tie(counter, rcs) = init_pass.write_buffer(
        "hi-z-spd-counter-zero", counter, TRANSFER_DST_BUFFER);
    init_pass.set_callback(
        [rcs](Renderer &renderer, const RgRuntime &rg, CommandRecorder &cmd) {
          cmd.fill_buffer(BufferView(rg.get_buffer(rcs)), 0);
        });
  }

  auto pass = ccfg.rgb->create_pass({.name = "hi-z-spd"});

  struct Resources {
    Handle<ComputePipeline> pipeline;
    RgTextureToken depth_buffer;
    RgTextureToken hi_z;
    RgBufferToken<glsl::uint> counter;
  } rcs;

  rcs.pipeline = ccfg.pipelines->hi_z;

  rcs.depth_buffer = pass.read_texture(cfg.depth_buffer, CS_SAMPLE_TEXTURE,
                                       ccfg.samplers->hi_z_gen);

  std::tie(*cfg.hi_z, rcs.hi_z) =
      pass.write_texture("hi-z", ccfg.rcs->hi_z, CS_WRITE_TEXTURE);

  std::tie(counter, rcs.counter) =
      pass.write_buffer("hi-z-spd-counter", counter, CS_READ_WRITE_BUFFER);

  pass.set_compute_callback([rcs, size, num_mips](Renderer &renderer,
                                                  const RgRuntime &rg,
                                                  ComputePass &cmd) {
    cmd.bind_compute_pipeline(rcs.pipeline);
    cmd.bind_descriptor_sets({rg.get_texture_set()});
    auto [descriptors, descriptors_ptr, _] =
        rg.allocate<glsl::RWStorageTexture2D>(num_mips);
    for (i32 mip = 0; mip < num_mips; ++mip) {
      descriptors[mip] = glsl::RWStorageTexture2D(
          rg.get_storage_texture_descriptor(rcs.hi_z, mip));
    }
    cmd.set_push_constants(glsl::HiZSpdPassArgs{
        .counter = rg.get_buffer_device_ptr(rcs.counter),
        .dsts = descriptors_ptr,
        .dst_size = size,
        .num_dst_mips = num_mips,
        .src = glsl::SampledTexture2D(
            rg.get_sampled_texture_descriptor(rcs.depth_buffer)),
    });
    cmd.dispatch_threads(
        size, {glsl::HI_Z_SPD_THREADS_X * glsl::HI_Z_SPD_THREAD_ELEMS_X,
               glsl::HI_Z_SPD_THREADS_Y * glsl::HI_Z_SPD_THREAD_ELEMS_Y});
  });
}

} // namespace ren
