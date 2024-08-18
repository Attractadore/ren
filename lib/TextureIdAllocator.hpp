#pragma once
#include "FreeListAllocator.hpp"
#include "Support/GenIndex.hpp"
#include "Support/NewType.hpp"

#include <vulkan/vulkan.h>

namespace ren {

class Renderer;

struct DescriptorSetLayout;
struct PipelineLayout;
struct Sampler;
struct TextureView;

REN_NEW_TYPE(SampledTextureId, unsigned);
REN_NEW_TYPE(StorageTextureId, unsigned);

class TextureIdAllocator {
  VkDescriptorSet m_set;
  Handle<DescriptorSetLayout> m_layout;
  FreeListAllocator m_sampler_allocator;
  FreeListAllocator m_sampled_texture_allocator;
  FreeListAllocator m_storage_texture_allocator;

public:
  TextureIdAllocator(VkDescriptorSet set, Handle<DescriptorSetLayout> layout);

  auto get_set() const -> VkDescriptorSet;

  auto get_set_layout() const -> Handle<DescriptorSetLayout>;

  auto allocate_sampled_texture(Renderer &renderer, const TextureView &view,
                                Handle<Sampler> sampler) -> SampledTextureId;

  void free_sampled_texture(SampledTextureId texture);

  auto allocate_storage_texture(Renderer &renderer,
                                const TextureView &view) -> StorageTextureId;

  void free_storage_texture(StorageTextureId texture);
};

class TextureIdAllocatorScope {
public:
  TextureIdAllocatorScope(TextureIdAllocator &alloc);
  TextureIdAllocatorScope(const TextureIdAllocatorScope &) = delete;
  TextureIdAllocatorScope(TextureIdAllocatorScope &&other) = default;
  ~TextureIdAllocatorScope();

  TextureIdAllocatorScope &operator=(const TextureIdAllocatorScope &) = delete;
  TextureIdAllocatorScope &operator=(TextureIdAllocatorScope &&other) noexcept;

  auto get_set() const -> VkDescriptorSet;

  auto get_set_layout() const -> Handle<DescriptorSetLayout>;

  auto allocate_sampled_texture(Renderer &renderer, const TextureView &view,
                                Handle<Sampler> sampler) -> SampledTextureId;

  auto allocate_storage_texture(Renderer &renderer,
                                const TextureView &view) -> StorageTextureId;

  void clear();

private:
  TextureIdAllocator *m_alloc = nullptr;
  Vector<SampledTextureId> m_sampled_textures;
  Vector<StorageTextureId> m_storage_textures;
};

}; // namespace ren
