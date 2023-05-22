#pragma once
#include "Descriptors.hpp"
#include "Pipeline.hpp"

namespace ren {

auto create_persistent_descriptor_set_layout(ResourceArena &arena)
    -> Handle<DescriptorSetLayout>;

auto create_color_pass_pipeline_layout(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<PipelineLayout>;

auto load_reinhard_tonemap_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<ComputePipeline>;

struct PostprocessingPipelines {
  Handle<ComputePipeline> reinhard_tonemap;
};

auto load_postprocessing_pipelines(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> PostprocessingPipelines;

} // namespace ren
