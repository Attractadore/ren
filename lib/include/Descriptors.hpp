#pragma once
#include "ShaderStages.hpp"
#include "Support/Enum.hpp"
#include "Support/Ref.hpp"
#include "Support/Vector.hpp"

#include <array>

namespace ren {

#define REN_DESCRIPTOR_POOL_OPTIONS (UpdateAfterBind)
REN_DEFINE_FLAGS_ENUM(DescriptorPoolOption, REN_DESCRIPTOR_POOL_OPTIONS);

#define REN_DESCRIPTORS                                                        \
  (Sampler)(SampledImage)(StorageImage)(UniformBuffer)(StorageBuffer)
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
  DescriptorCounts descriptor_counts;
};

struct DescriptorPoolRef {
  DescriptorPoolDesc desc;
  void *handle;
};

struct DescriptorPool {
  DescriptorPoolDesc desc;
  AnyRef handle;
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
  SmallVector<DescriptorSetBinding, 8> bindings;
};

struct DescriptorSetLayoutRef {
  DescriptorSetLayoutDesc desc;
  void *handle;
};

struct DescriptorSetLayout {
  DescriptorSetLayoutDesc desc;
  AnyRef handle;
};

struct DescriptorSetDesc {};

struct DescriptorSetRef {
  DescriptorSetDesc desc;
  void *handle;
};

struct DescriptorSet {
  DescriptorSetDesc desc;
  AnyRef handle;
};

} // namespace ren
