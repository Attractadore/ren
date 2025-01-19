#pragma once
#include "DebugNames.hpp"
#include "rhi.hpp"

namespace ren {

struct ResourceDescriptorHeapCreateInfo {
  REN_DEBUG_NAME_FIELD("Resource descriptor heap");
  union {
    struct {
      u32 num_srv_descriptors = 0;
      u32 num_cis_descriptors = 0;
      u32 num_uav_descriptors = 0;
    };
    std::array<u32, 3> num_descriptors;
  };
};

struct SamplerDescriptorHeapCreateInfo {
  REN_DEBUG_NAME_FIELD("Sampler descriptor heap");
};

struct ResourceDescriptorHeap {
  rhi::ResourceDescriptorHeap handle;
  union {
    struct {
      u32 num_srv_descriptors = 0;
      u32 num_cis_descriptors = 0;
      u32 num_uav_descriptors = 0;
    };
    std::array<u32, 3> num_descriptors;
  };
};

struct SamplerDescriptorHeap {
  rhi::SamplerDescriptorHeap handle;
};

} // namespace ren
