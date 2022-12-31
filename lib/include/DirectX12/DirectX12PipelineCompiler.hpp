#pragma once
#include "PipelineCompiler.hpp"

namespace ren {

class DirectX12Device;

class DirectX12PipelineCompiler final : public PipelineCompiler {
  DirectX12Device *m_device;

private:
  virtual Pipeline compile_pipeline(const PipelineConfig &config) override;

public:
  DirectX12PipelineCompiler(DirectX12Device &device);
};

} // namespace ren
