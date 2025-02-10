#pragma once
#include "DebugNames.hpp"
#include "rhi.hpp"

#include <array>

namespace ren {

struct ResourceDescriptorHeapCreateInfo {
  REN_DEBUG_NAME_FIELD("Resource descriptor heap");
  union {
    struct {
      u32 num_srv_descriptors;
      u32 num_cis_descriptors;
      u32 num_uav_descriptors;
    };
    std::array<u32, 3> num_descriptors = {};
  };
};

struct SamplerDescriptorHeapCreateInfo {
  REN_DEBUG_NAME_FIELD("Sampler descriptor heap");
};

struct ResourceDescriptorHeap {
  rhi::ResourceDescriptorHeap handle;
  union {
    struct {
      u32 num_srv_descriptors;
      u32 num_cis_descriptors;
      u32 num_uav_descriptors;
    };
    std::array<u32, 3> num_descriptors = {};
  };
};

struct SamplerDescriptorHeap {
  rhi::SamplerDescriptorHeap handle;
};

} // namespace ren
