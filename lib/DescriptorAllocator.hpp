#pragma once
#include "FreeListAllocator.hpp"
#include "PipelineLoading.hpp"
#include "core/GenIndex.hpp"
#include "core/Result.hpp"
#include "core/Span.hpp"
#include "glsl/Texture.h"
#include "ren/ren.hpp"

#include <vulkan/vulkan.h>

namespace ren {

class Renderer;

struct DescriptorSetLayout;
struct Sampler;
struct SrvDesc;
struct UavDesc;

class DescriptorAllocator {
  std::array<VkDescriptorSet, 4> m_sets = {};
  PersistentDescriptorSetLayouts m_layouts = {};
  FreeListAllocator m_srvs;
  FreeListAllocator m_ciss;
  FreeListAllocator m_uavs;
  FreeListAllocator m_samplers;

public:
  DescriptorAllocator(Span<const VkDescriptorSet> sets,
                      const PersistentDescriptorSetLayouts &layouts);

  auto get_sets() const -> Span<const VkDescriptorSet>;

  auto allocate_sampler(Renderer &renderer, Handle<Sampler> sampler)
      -> glsl::SamplerState;

  auto try_allocate_sampler(Renderer &renderer, Handle<Sampler> sampler,
                            glsl::SamplerState id) -> glsl::SamplerState;

  auto allocate_sampler(Renderer &renderer, Handle<Sampler> sampler,
                        glsl::SamplerState id) -> glsl::SamplerState;

  void free_sampler(glsl::SamplerState sampler);

  auto allocate_texture(Renderer &renderer, SrvDesc srv)
      -> Result<glsl::Texture, Error>;

  void free_texture(glsl::Texture texture);

  auto allocate_sampled_texture(Renderer &renderer, SrvDesc srv,
                                Handle<Sampler> sampler)
      -> Result<glsl::SampledTexture, Error>;

  void free_sampled_texture(glsl::SampledTexture texture);

  auto allocate_storage_texture(Renderer &renderer, UavDesc uav)
      -> Result<glsl::StorageTexture, Error>;

  void free_storage_texture(glsl::StorageTexture texture);
};

class DescriptorAllocatorScope {
public:
  DescriptorAllocatorScope(DescriptorAllocator &alloc);
  DescriptorAllocatorScope(const DescriptorAllocatorScope &) = delete;
  DescriptorAllocatorScope(DescriptorAllocatorScope &&other) = default;
  ~DescriptorAllocatorScope();

  DescriptorAllocatorScope &
  operator=(const DescriptorAllocatorScope &) = delete;
  DescriptorAllocatorScope &
  operator=(DescriptorAllocatorScope &&other) noexcept;

  auto get_sets() const -> Span<const VkDescriptorSet>;

  auto allocate_sampler(Renderer &renderer, Handle<Sampler> sampler)
      -> glsl::SamplerState;

  auto allocate_texture(Renderer &renderer, SrvDesc srv)
      -> Result<glsl::Texture, Error>;

  auto allocate_sampled_texture(Renderer &renderer, SrvDesc srv,
                                Handle<Sampler> sampler)
      -> Result<glsl::SampledTexture, Error>;

  auto allocate_storage_texture(Renderer &renderer, UavDesc uav)
      -> Result<glsl::StorageTexture, Error>;

  void reset();

private:
  DescriptorAllocator *m_alloc = nullptr;
  Vector<glsl::Texture> m_srvs;
  Vector<glsl::SampledTexture> m_ciss;
  Vector<glsl::StorageTexture> m_uavs;
  Vector<glsl::SamplerState> m_samplers;
};

}; // namespace ren
