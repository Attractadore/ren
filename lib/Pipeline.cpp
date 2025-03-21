#include "Pipeline.hpp"
#include "ResourceArena.hpp"
#include "core/Errors.hpp"

#include <fmt/format.h>
#include <spirv_reflect.h>

namespace ren {

auto create_pipeline_layout(ResourceArena &arena,
                            TempSpan<const Span<const std::byte>> shaders,
                            StringView name)
    -> Result<Handle<PipelineLayout>, Error> {
  u32 push_constants_size = 0;

  SmallVector<SpvReflectDescriptorSet *> sets;
  for (auto code : shaders) {
    spv_reflect::ShaderModule shader(code.size_bytes(), code.data(),
                                     SPV_REFLECT_MODULE_FLAG_NO_COPY);
    throw_if_failed(shader.GetResult(),
                    "SPIRV-Reflect: Failed to create shader module");

    u32 num_pc_blocks = 0;
    throw_if_failed(shader.EnumeratePushConstantBlocks(&num_pc_blocks, nullptr),
                    "SPIRV-Reflect: Failed to enumerate push constants");
    ren_assert(num_pc_blocks <= 1);
    if (num_pc_blocks) {
      SpvReflectBlockVariable *block_var;
      throw_if_failed(
          shader.EnumeratePushConstantBlocks(&num_pc_blocks, &block_var),
          "SPIRV-Reflect: Failed to enumerate push constants");
      push_constants_size = block_var->padded_size;
    }
  }

  return arena.create_pipeline_layout({
      .name = fmt::format("{} pipeline layout", name),
      .use_resource_heap = true,
      .use_sampler_heap = true,
      .push_constants_size = push_constants_size,
  });
}

#undef load_compute_pipeline
auto load_compute_pipeline(ResourceArena &arena, Span<const std::byte> shader,
                           StringView name)
    -> Result<Handle<ComputePipeline>, Error> {
  ren_try(Handle<PipelineLayout> layout,
          create_pipeline_layout(arena, {shader}, name));
  return arena.create_compute_pipeline({
      .name = fmt::format("{} compute pipeline", name),
      .layout = layout,
      .cs = {shader},
  });
};

} // namespace ren
