#pragma once
#include "DebugNames.hpp"
#include "core/GenIndex.hpp"
#include "core/Span.hpp"
#include "rhi.hpp"

#include <glm/glm.hpp>

namespace ren {

struct DynamicState {};
constexpr DynamicState DYNAMIC;

struct SpecializationConstant {
  u32 id = 0;
  u32 value = 0;
};

struct ShaderInfo {
  Span<const std::byte> code;
  const char *entry_point = "main";
  TempSpan<const SpecializationConstant> specialization_constants;
};

struct GraphicsPipelineCreateInfo {
  REN_DEBUG_NAME_FIELD("Graphics pipeline");
  ShaderInfo ts;
  ShaderInfo ms;
  ShaderInfo vs;
  ShaderInfo fs;
  rhi::InputAssemblyStateInfo input_assembly_state;
  rhi::RasterizationStateInfo rasterization_state;
  rhi::MultisamplingStateInfo multisampling_state;
  rhi::DepthStencilStateInfo depth_stencil_state;
  TinyImageFormat rtv_formats[rhi::MAX_NUM_RENDER_TARGETS] = {};
  TinyImageFormat dsv_format = TinyImageFormat_UNDEFINED;
  rhi::BlendStateInfo blend_state;
};

struct GraphicsPipeline {
  rhi::Pipeline handle;
};

struct ComputePipelineCreateInfo {
  REN_DEBUG_NAME_FIELD("Compute pipeline");
  ShaderInfo cs;
};

struct ComputePipeline {
  rhi::Pipeline handle;
  glm::uvec3 local_size = {};
};

class ResourceArena;

auto load_compute_pipeline(Arena scratch, ResourceArena &arena,
                           Span<const std::byte> shader, StringView name)
    -> Result<Handle<ComputePipeline>, Error>;

#define load_compute_pipeline(scratch, arena, shader, name)                    \
  load_compute_pipeline(scratch, arena, Span(shader, shader##Size).as_bytes(), \
                        name)

} // namespace ren
