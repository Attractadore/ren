#pragma once
#include "ResourceArena.hpp"
#include "core/GenIndex.hpp"
#include "glsl/Mesh.h"

namespace ren {

struct PersistentDescriptorSetLayouts {
  Handle<DescriptorSetLayout> srv;
  Handle<DescriptorSetLayout> cis;
  Handle<DescriptorSetLayout> uav;
  Handle<DescriptorSetLayout> sampler;
};

auto create_persistent_descriptor_set_layout(ResourceArena &arena)
    -> PersistentDescriptorSetLayouts;

struct Pipelines {
  Handle<ComputePipeline> instance_culling_and_lod;
  Handle<ComputePipeline> meshlet_culling;
  Handle<ComputePipeline> exclusive_scan_uint32;
  Handle<ComputePipeline> meshlet_sorting;
  Handle<ComputePipeline> prepare_batch;
  Handle<ComputePipeline> hi_z;
  Handle<GraphicsPipeline> early_z_pass;
  std::array<Handle<GraphicsPipeline>, glsl::NUM_MESH_ATTRIBUTE_FLAGS>
      opaque_pass;
  Handle<ComputePipeline> post_processing;
  Handle<ComputePipeline> reduce_luminance_histogram;
  Handle<GraphicsPipeline> imgui_pass;
};

auto load_pipelines(
    ResourceArena &arena,
    const PersistentDescriptorSetLayouts &persistent_set_layouts) -> Pipelines;

} // namespace ren
