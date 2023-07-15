#pragma once
#include "Descriptors.hpp"
#include "Pipeline.hpp"

namespace ren {

auto create_persistent_descriptor_set_layout(ResourceArena &arena)
    -> Handle<DescriptorSetLayout>;

struct Pipelines {
  Handle<GraphicsPipeline> opaque_pass;
  Handle<ComputePipeline> post_processing;
  Handle<ComputePipeline> reduce_luminance_histogram;
};

auto load_pipelines(ResourceArena &arena,
                    Handle<DescriptorSetLayout> persistent_set_layout)
    -> Pipelines;

auto load_opaque_pass_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<GraphicsPipeline>;

auto load_post_processing_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<ComputePipeline>;

auto load_reduce_luminance_histogram_pipeline(ResourceArena &arena)
    -> Handle<ComputePipeline>;

} // namespace ren
