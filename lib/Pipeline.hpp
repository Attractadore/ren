#pragma once
#include "DebugNames.hpp"
#include "core/GenIndex.hpp"
#include "core/Span.hpp"
#include "core/Vector.hpp"
#include "rhi.hpp"

#include <glm/glm.hpp>

namespace ren {

struct DescriptorSetLayout;

struct DynamicState {};
constexpr DynamicState DYNAMIC;

struct PipelineLayoutCreateInfo {
  REN_DEBUG_NAME_FIELD("Pipeline layout");
  bool use_resource_heap = false;
  bool use_sampler_heap = false;
  TempSpan<const rhi::PushDescriptor> push_descriptors = {};
  u32 push_constants_size = 0;
};

struct PipelineLayout {
  rhi::PipelineLayout handle;
  bool use_resource_heap = false;
  bool use_sampler_heap = false;
  Vector<rhi::PushDescriptor> push_descriptors;
  u32 push_constants_size = 0;
};

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
  Handle<PipelineLayout> layout;
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
  Handle<PipelineLayout> layout;
};

struct ComputePipelineCreateInfo {
  REN_DEBUG_NAME_FIELD("Compute pipeline");
  Handle<PipelineLayout> layout;
  ShaderInfo cs;
};

struct ComputePipeline {
  rhi::Pipeline handle;
  Handle<PipelineLayout> layout;
  glm::uvec3 local_size = {};
};

class ResourceArena;

auto create_pipeline_layout(ResourceArena &arena,
                            TempSpan<const Span<const std::byte>> shaders,
                            StringView name)
    -> Result<Handle<PipelineLayout>, Error>;

auto load_compute_pipeline(ResourceArena &arena, Span<const std::byte> shader,
                           StringView name)
    -> Result<Handle<ComputePipeline>, Error>;

#define load_compute_pipeline(arena, shader, name)                             \
  load_compute_pipeline(arena, Span(shader, shader##Size).as_bytes(), name)

} // namespace ren
