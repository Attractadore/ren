#pragma once
#include "FreeListAllocator.hpp"
#include "Support/GenIndex.hpp"
#include "glsl/Texture.h"

#include <vulkan/vulkan.h>

namespace ren {

class Renderer;

struct DescriptorSetLayout;
struct Sampler;
struct TextureView;

class DescriptorAllocator {
  VkDescriptorSet m_set = nullptr;
  Handle<DescriptorSetLayout> m_layout;
  FreeListAllocator m_samplers;
  FreeListAllocator m_textures;
  FreeListAllocator m_sampled_textures;
  FreeListAllocator m_storage_textures;

public:
  DescriptorAllocator(VkDescriptorSet set, Handle<DescriptorSetLayout> layout);

  auto get_set() const -> VkDescriptorSet;

  auto get_set_layout() const -> Handle<DescriptorSetLayout>;

  auto allocate_sampler(Renderer &renderer,
                        Handle<Sampler> sampler) -> glsl::SamplerState;

  auto try_allocate_sampler(Renderer &renderer, Handle<Sampler> sampler,
                            glsl::SamplerState id) -> glsl::SamplerState;

  auto allocate_sampler(Renderer &renderer, Handle<Sampler> sampler,
                        glsl::SamplerState id) -> glsl::SamplerState;

  void free_sampler(glsl::SamplerState sampler);

  auto allocate_texture(Renderer &renderer,
                        const TextureView &view) -> glsl::Texture;

  void free_texture(glsl::Texture texture);

  auto
  allocate_sampled_texture(Renderer &renderer, const TextureView &view,
                           Handle<Sampler> sampler) -> glsl::SampledTexture;

  void free_sampled_texture(glsl::SampledTexture texture);

  auto allocate_storage_texture(Renderer &renderer, const TextureView &view)
      -> glsl::RWStorageTexture;

  void free_storage_texture(glsl::RWStorageTexture texture);
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

  auto get_set() const -> VkDescriptorSet;

  auto get_set_layout() const -> Handle<DescriptorSetLayout>;

  auto allocate_sampler(Renderer &renderer,
                        Handle<Sampler> sampler) -> glsl::SamplerState;

  auto allocate_texture(Renderer &renderer,
                        const TextureView &view) -> glsl::Texture;

  auto
  allocate_sampled_texture(Renderer &renderer, const TextureView &view,
                           Handle<Sampler> sampler) -> glsl::SampledTexture;

  auto allocate_storage_texture(Renderer &renderer, const TextureView &view)
      -> glsl::RWStorageTexture;

  void reset();

private:
  DescriptorAllocator *m_alloc = nullptr;
  Vector<glsl::SamplerState> m_samplers;
  Vector<glsl::Texture> m_textures;
  Vector<glsl::SampledTexture> m_sampled_textures;
  Vector<glsl::RWStorageTexture> m_storage_textures;
};

}; // namespace ren
