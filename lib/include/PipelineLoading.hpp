#pragma once
#include "Descriptors.hpp"
#include "Pipeline.hpp"

namespace ren {

auto create_persistent_descriptor_set_layout(ResourceArena &arena)
    -> Handle<DescriptorSetLayout>;

struct Pipelines {
  Handle<GraphicsPipeline> color_pass;
  Handle<ComputePipeline> build_luminance_histogram;
  Handle<ComputePipeline> reduce_luminance_histogram;
  Handle<ComputePipeline> reinhard_tone_mapping;
};

auto load_pipelines(ResourceArena &arena,
                    Handle<DescriptorSetLayout> persistent_set_layout)
    -> Pipelines;

auto load_color_pass_pipeline(ResourceArena &arena,
                              Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<GraphicsPipeline>;

auto load_reinhard_tone_mapping_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<ComputePipeline>;

auto load_build_luminance_histogram_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<ComputePipeline>;

auto load_reduce_luminance_histogram_pipeline(ResourceArena &arena)
    -> Handle<ComputePipeline>;

} // namespace ren
