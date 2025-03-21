#include "BumpAllocator.hpp"
#include "RenderGraph.hpp"
#include "ResourceArena.hpp"

namespace ren {

struct BakerPipelines {
  Handle<ComputePipeline> dhr_lut;
};

struct IBaker {
  Renderer *renderer = nullptr;
  ResourceArena arena;
  ResourceArena bake_arena;
  Handle<CommandPool> cmd_pool;
  RgPersistent rg;
  DescriptorAllocator descriptor_allocator;
  DescriptorAllocatorScope bake_descriptor_allocator;
  DeviceBumpAllocator allocator;
  UploadBumpAllocator upload_allocator;
  BakerPipelines pipelines;
};

void reset_baker(IBaker &baker);

} // namespace ren
