#pragma once
#include "ResourceArena.hpp"
#include "core/GenIndex.hpp"
#include "sh/Geometry.h"

namespace ren {

struct Pipelines {
  Handle<ComputePipeline> instance_culling_and_lod;
  Handle<ComputePipeline> meshlet_culling;
  Handle<ComputePipeline> exclusive_scan_uint32;
  Handle<ComputePipeline> meshlet_sorting;
  Handle<ComputePipeline> prepare_batch;
  Handle<ComputePipeline> hi_z;
  Handle<ComputePipeline> ssao_hi_z;
  Handle<ComputePipeline> ssao;
  Handle<ComputePipeline> ssao_filter;
  Handle<GraphicsPipeline> early_z_pass;
  std::array<Handle<GraphicsPipeline>, sh::NUM_MESH_ATTRIBUTE_FLAGS>
      opaque_pass;
  Handle<GraphicsPipeline> skybox_pass;
  Handle<ComputePipeline> local_tone_mapping;
  Handle<ComputePipeline> local_tone_mapping_accumulate;
  Handle<ComputePipeline> post_processing;
  Handle<ComputePipeline> reduce_luminance_histogram;
  Handle<GraphicsPipeline> imgui_pass;
};

auto load_pipelines(ResourceArena &arena) -> Result<Pipelines, Error>;

} // namespace ren
