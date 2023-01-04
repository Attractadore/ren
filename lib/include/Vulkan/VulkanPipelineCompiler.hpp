#pragma once
#include "PipelineCompiler.hpp"

#include <vulkan/vulkan.h>

namespace ren {

class VulkanDevice;

class VulkanPipelineCompiler final : public PipelineCompiler {
  VulkanDevice *m_device;
  SmallVector<VkDescriptorSetLayout, 4> m_set_layouts;
  VkPipelineLayout m_pipeline_layout;

private:
  virtual Pipeline compile_pipeline(const PipelineConfig &config) override;

  void destroy();

public:
  VulkanPipelineCompiler(VulkanDevice &device);
  VulkanPipelineCompiler(const VulkanPipelineCompiler &) = delete;
  VulkanPipelineCompiler(VulkanPipelineCompiler &&);
  VulkanPipelineCompiler &operator=(const VulkanPipelineCompiler &) = delete;
  VulkanPipelineCompiler &operator=(VulkanPipelineCompiler &&);
  ~VulkanPipelineCompiler();
};

} // namespace ren
