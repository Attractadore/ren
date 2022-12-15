#include "Scene.hpp"
#include "CommandAllocator.hpp"
#include "Device.hpp"
#include "RenderGraph.hpp"
#include "Support/Array.hpp"

#include <range/v3/algorithm.hpp>
#include <range/v3/range.hpp>
#include <range/v3/view.hpp>

using namespace ren;

Scene::RenScene(Device *device) : m_device(device) { setPipelineDepth(2); }

Scene::~RenScene() = default;

void Scene::setPipelineDepth(unsigned depth) {
  m_cmd_pool = m_device->createCommandBufferPool(depth);
}

void Scene::setOutputSize(unsigned width, unsigned height) {
  m_output_width = width;
  m_output_height = height;
}

void Scene::setSwapchain(Swapchain *swapchain) { m_swapchain = swapchain; }

void Scene::draw() {
  auto rgb = m_device->createRenderGraphBuilder();

  // Draw scene
  auto draw = rgb->addNode();
  draw.setDesc("Color pass");
  RGTextureDesc rt_desc = {
      .format = Format::RGBA16F,
      .width = m_output_width,
      .height = m_output_height,
  };
  auto rt = draw.addOutput(rt_desc, MemoryAccess::ColorWrite,
                           PipelineStage::ColorOutput);
  rgb->setDesc(rt, "Color buffer");
  draw.setCallback([=](CommandBuffer &cmd, RenderGraph &rg) {
    cmd.beginRendering(rg.getTexture(rt));
    cmd.endRendering();
  });

  // Post-process
  auto pp = rgb->addNode();
  pp.setDesc("Post-process pass");
  auto pprt = pp.addWriteInput(
      rt, MemoryAccess::StorageRead | MemoryAccess::StorageWrite,
      PipelineStage::Compute);
  rgb->setDesc(pprt, "Post-processed color buffer");
  pp.setCallback([](CommandBuffer &cmd, RenderGraph &rg) {});

  // Present to swapchain
  rgb->setSwapchain(m_swapchain);
  rgb->setFinalImage(pprt);

  auto rg = rgb->build();

  m_cmd_pool->beginFrame();
  rg->execute(m_cmd_pool.get());
  m_cmd_pool->endFrame();
}
