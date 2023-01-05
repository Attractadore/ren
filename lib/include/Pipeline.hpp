#pragma once
#include "Support/Ref.hpp"

namespace ren {

#define REN_SHADER_STAGES (Vertex)(Fragment)(Compute)
REN_DEFINE_FLAGS_ENUM(ShaderStage, REN_SHADER_STAGES);

struct PipelineDesc {};

struct PipelineRef {
  PipelineDesc desc;
  void *handle;

  void *get() const { return handle; }
};

struct Pipeline {
  PipelineDesc desc;
  AnyRef handle;

  operator PipelineRef() const {
    return {.desc = desc, .handle = handle.get()};
  }

  void *get() const { return handle.get(); }
};

template <typename T>
concept PipelineLike =
    std::same_as<Pipeline, T> or std::same_as<PipelineRef, T>;

struct PipelineSignatureDesc {};

struct PipelineSignatureRef {
  PipelineSignatureDesc desc;
  void *handle;

  void *get() const { return handle; }
};

struct PipelineSignature {
  PipelineSignatureDesc desc;
  AnyRef handle;

  void *get() const { return handle.get(); }
};

template <typename T>
concept PipelineSignatureLike =
    std::same_as<PipelineSignature, T> or std::same_as<PipelineSignatureRef, T>;

} // namespace ren
