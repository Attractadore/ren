#include "BumpAllocator.hpp"
#include "RenderGraph.hpp"
#include "ResourceArena.hpp"
#include "ResourceUploader.hpp"

namespace ren {

struct BakerPipelines {
  Handle<ComputePipeline> so_lut;
  Handle<ComputePipeline> reflection_map;
  Handle<ComputePipeline> specular_map;
  Handle<ComputePipeline> irradiance_map;
};

struct IBaker {
  Renderer *renderer = nullptr;
  ResourceArena session_arena;
  ResourceArena arena;
  Handle<CommandPool> cmd_pool;
  RgPersistent rg;
  DescriptorAllocator session_descriptor_allocator;
  DescriptorAllocatorScope descriptor_allocator;
  DeviceBumpAllocator allocator;
  UploadBumpAllocator upload_allocator;
  ResourceUploader uploader;
  BakerPipelines pipelines;
};

void reset_baker(IBaker &baker);

} // namespace ren
