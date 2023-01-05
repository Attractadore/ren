#pragma once
#include "PipelineCompiler.hpp"

#include <vulkan/vulkan.h>

namespace ren {

class VulkanDevice;

class VulkanPipelineCompiler final : public PipelineCompiler {
  VulkanDevice *m_device;
  SmallVector<VkDescriptorSetLayout, 4> m_set_layouts;

private:
  static auto create(VulkanDevice &device) -> VulkanPipelineCompiler;

  VulkanPipelineCompiler(VulkanDevice &device,
                         SmallVector<VkDescriptorSetLayout, 4> set_layouts,
                         VkPipelineLayout layout);
  void destroy();

  virtual Pipeline compile_pipeline(const PipelineConfig &config) override;

public:
  VulkanPipelineCompiler(VulkanDevice &device);
  VulkanPipelineCompiler(const VulkanPipelineCompiler &) = delete;
  VulkanPipelineCompiler(VulkanPipelineCompiler &&) = default;
  VulkanPipelineCompiler &operator=(const VulkanPipelineCompiler &) = delete;
  VulkanPipelineCompiler &operator=(VulkanPipelineCompiler &&);
  ~VulkanPipelineCompiler();
};

} // namespace ren
