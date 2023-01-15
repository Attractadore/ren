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

#define REN_DESCRIPTOR_TYPES                                                   \
  (DESCRIPTOR_TYPE_SAMPLER)                  /**/                              \
      (DESCRIPTOR_TYPE_TEXTURE)              /* Texture */                     \
      (DESCRIPTOR_TYPE_RW_TEXTURE)           /* RWTexture */                   \
      (DESCRIPTOR_TYPE_TEXEL_BUFFER)         /* Buffer */                      \
      (DESCRIPTOR_TYPE_RW_TEXEL_BUFFER)      /* RWBuffer */                    \
      (DESCRIPTOR_TYPE_UNIFORM_BUFFER)       /* ConstantBuffer */              \
      (DESCRIPTOR_TYPE_STRUCTURED_BUFFER)    /**/                              \
      (DESCRIPTOR_TYPE_RW_STRUCTURED_BUFFER) /* RWStructuredBuffer,            \
                                                AppendStructuredBuffer and     \
                                                ConsumeStructuredBuffer */     \
      (DESCRIPTOR_TYPE_RAW_BUFFER)           /* ByteAddressBuffer */           \
      (DESCRIPTOR_TYPE_RW_RAW_BUFFER)        /* RWByteAddressBuffer */
REN_DEFINE_C_ENUM(DescriptorType, REN_DESCRIPTOR_TYPES);
constexpr auto DESCRIPTOR_TYPE_COUNT = BOOST_PP_SEQ_SIZE(REN_DESCRIPTOR_TYPES);

struct DescriptorPoolDesc {
  DescriptorPoolOptionFlags flags;
  unsigned set_count;
  std::array<unsigned, DESCRIPTOR_TYPE_COUNT> pool_sizes;
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
REN_DEFINE_FLAGS_ENUM(DescriptorBindingOption,
                      REN_DESCRIPTOR_SET_BINDING_OPTIONS);

struct DescriptorBinding {
  DescriptorBindingOptionFlags flags;
  unsigned binding;
  DescriptorType type;
  unsigned count;
  ShaderStageFlags stages;
};

struct DescriptorSetLayoutDesc {
  DescriptorSetLayoutOptionFlags flags;
  Vector<DescriptorBinding> bindings;
};

struct DescriptorSetLayoutRef {
  DescriptorSetLayoutDesc *desc;
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
  void *handle;
};

template <DescriptorType DT> struct DescriptorWriteConfig;

template <> struct DescriptorWriteConfig<DESCRIPTOR_TYPE_SAMPLER> {
  // TODO
};

template <> struct DescriptorWriteConfig<DESCRIPTOR_TYPE_TEXTURE> {
  std::span<const TextureView> textures;
};

template <> struct DescriptorWriteConfig<DESCRIPTOR_TYPE_RW_TEXTURE> {
  std::span<const RWTextureView> textures;
};

template <DescriptorType DT>
  requires(DT == DESCRIPTOR_TYPE_TEXEL_BUFFER) or
          (DT == DESCRIPTOR_TYPE_RW_TEXEL_BUFFER)
struct DescriptorWriteConfig<DT> {
  // TODO
};

template <DescriptorType DT>
  requires(DT == DESCRIPTOR_TYPE_UNIFORM_BUFFER) or
          (DT == DESCRIPTOR_TYPE_RAW_BUFFER) or
          (DT == DESCRIPTOR_TYPE_RW_RAW_BUFFER)
struct DescriptorWriteConfig<DT> {
  std::span<const BufferRef> buffers;
};

template <> struct DescriptorWriteConfig<DESCRIPTOR_TYPE_STRUCTURED_BUFFER> {
  std::span<const BufferRef> buffers;
  std::span<const unsigned> strides;
};

template <> struct DescriptorWriteConfig<DESCRIPTOR_TYPE_RW_STRUCTURED_BUFFER> {
  std::span<const BufferRef> buffers;
  std::span<const BufferRef> counters;
  std::span<const unsigned> strides;
};

using SamplerWriteConfig = DescriptorWriteConfig<DESCRIPTOR_TYPE_SAMPLER>;
using TextureWriteConfig = DescriptorWriteConfig<DESCRIPTOR_TYPE_TEXTURE>;
using RWTextureWriteConfig = DescriptorWriteConfig<DESCRIPTOR_TYPE_RW_TEXTURE>;
using TexelBufferWriteConfig =
    DescriptorWriteConfig<DESCRIPTOR_TYPE_TEXEL_BUFFER>;
using RWTexelBufferWriteConfig =
    DescriptorWriteConfig<DESCRIPTOR_TYPE_RW_TEXEL_BUFFER>;
using UniformBufferWriteConfig =
    DescriptorWriteConfig<DESCRIPTOR_TYPE_UNIFORM_BUFFER>;
using StructuredBufferWriteConfig =
    DescriptorWriteConfig<DESCRIPTOR_TYPE_STRUCTURED_BUFFER>;
using RWStructuredBufferWriteConfig =
    DescriptorWriteConfig<DESCRIPTOR_TYPE_RW_STRUCTURED_BUFFER>;
using RawBufferWriteConfig = DescriptorWriteConfig<DESCRIPTOR_TYPE_RAW_BUFFER>;
using RWRawBufferWriteConfig =
    DescriptorWriteConfig<DESCRIPTOR_TYPE_RW_RAW_BUFFER>;

struct DescriptorSetWriteConfig {
  DescriptorSet set;
  unsigned binding;
  unsigned array_index = 0;
#define define_variant(s, data, e) DescriptorWriteConfig<DescriptorType::e>
  std::variant<BOOST_PP_SEQ_ENUM(
      BOOST_PP_SEQ_TRANSFORM(define_variant, ~, REN_DESCRIPTOR_TYPES))>
      data;
#undef define_variant
};

} // namespace ren
