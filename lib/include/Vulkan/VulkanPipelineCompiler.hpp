#pragma once
#include "PipelineCompiler.hpp"

namespace ren {

class VulkanDevice;

class VulkanPipelineCompiler final : public PipelineCompiler {
  VulkanDevice *m_device;

private:
  virtual Pipeline compile_pipeline(const PipelineConfig &config) override;

public:
  VulkanPipelineCompiler(VulkanDevice &device);
};

} // namespace ren
