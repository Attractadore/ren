#pragma once
#include "Buffer.hpp"
#include "ShaderStages.hpp"
#include "Support/Enum.hpp"
#include "Support/Ref.hpp"
#include "Support/Vector.hpp"
#include "Texture.hpp"

#include <array>
#include <variant>

namespace ren {

#define REN_DESCRIPTOR_POOL_OPTIONS (UpdateAfterBind)
REN_DEFINE_FLAGS_ENUM(DescriptorPoolOption, REN_DESCRIPTOR_POOL_OPTIONS);

#define REN_DESCRIPTORS                                                        \
  (Sampler)(SampledTexture)(StorageTexture)(UniformBuffer)(StorageBuffer)
REN_DEFINE_ENUM(Descriptor, REN_DESCRIPTORS);

class DescriptorCounts {
  std::array<unsigned, BOOST_PP_SEQ_SIZE(REN_DESCRIPTORS)> m_storage = {};

public:
  const auto &operator[](Descriptor idx) const {
    return m_storage[static_cast<size_t>(idx)];
  }

  auto &operator[](Descriptor idx) {
    return m_storage[static_cast<size_t>(idx)];
  }
};

struct DescriptorPoolDesc {
  DescriptorPoolOptionFlags flags;
  unsigned set_count;
  DescriptorCounts descriptor_counts;
};

struct DescriptorPoolRef {
  DescriptorPoolDesc desc;
  void *handle;
};

struct DescriptorPool {
  DescriptorPoolDesc desc;
  AnyRef handle;

  operator DescriptorPoolRef() const {
    return {.desc = desc, .handle = handle.get()};
  }
};

#define REN_DESCRIPTOR_SET_LAYOUT_OPTIONS (UpdateAfterBind)
REN_DEFINE_FLAGS_ENUM(DescriptorSetLayoutOption,
                      REN_DESCRIPTOR_SET_LAYOUT_OPTIONS);

#define REN_DESCRIPTOR_SET_BINDING_OPTIONS                                     \
  (UpdateAfterBind)(UpdateUnusedWhilePending)(                                 \
      PartiallyBound)(VariableDescriptorCount)
REN_DEFINE_FLAGS_ENUM(DescriptorSetBindingOption,
                      REN_DESCRIPTOR_SET_BINDING_OPTIONS);

struct DescriptorSetBinding {
  DescriptorSetBindingOptionFlags flags;
  unsigned binding;
  Descriptor type;
  unsigned count;
  ShaderStageFlags stages;
};

struct DescriptorSetLayoutDesc {
  DescriptorSetLayoutOptionFlags flags;
  Vector<DescriptorSetBinding> bindings;
};

struct DescriptorSetLayoutRef {
  DescriptorSetLayoutDesc* desc;
  void *handle;
};

struct DescriptorSetLayout {
  std::shared_ptr<DescriptorSetLayoutDesc> desc;
  AnyRef handle;

  operator DescriptorSetLayoutRef() const {
    return {.desc = desc.get(), .handle = handle.get()};
  }
};

struct DescriptorSetDesc {};

struct DescriptorSet {
  DescriptorSetDesc desc;
  void* handle;
};

struct SamplerDescriptors {};

struct UniformBufferDescriptors {
  std::span<const BufferRef> buffers;
};

struct StorageBufferDescriptors {
  std::span<const BufferRef> buffers;
};

struct SampledTextureDescriptors {
  std::span<const SampledTextureView> textures;
};

struct StorageTextureDescriptors {
  std::span<const StorageTextureView> textures;
};

struct DescriptorSetWriteConfig {
  DescriptorSet set;
  unsigned binding;
  unsigned array_index = 0;
  std::variant<SamplerDescriptors, UniformBufferDescriptors,
               StorageBufferDescriptors, SampledTextureDescriptors,
               StorageTextureDescriptors>
      data;
};

} // namespace ren
