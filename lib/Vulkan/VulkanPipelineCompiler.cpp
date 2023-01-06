#include "Vulkan/VulkanPipelineCompiler.hpp"
#include "FragmentShaderSpvReflect.h"
#include "Support/Views.hpp"
#include "VertexShaderSpvReflect.h"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanFormats.hpp"
#include "Vulkan/VulkanPipeline.hpp"
#include "hlsl/cpp_interface.hlsl"

#include <spirv_reflect.h>

namespace ren {

namespace {

spv_reflect::ShaderModule reflect_vs() {
  spv_reflect::ShaderModule vs(sizeof(VertexShaderSpvReflect),
                               VertexShaderSpvReflect,
                               SPV_REFLECT_MODULE_FLAG_NO_COPY);
  throwIfFailed(vs.GetResult(),
                "SPIRV-Reflect: Failed to create shader module");
  return vs;
}

spv_reflect::ShaderModule reflect_fs() {
  spv_reflect::ShaderModule fs(sizeof(FragmentShaderSpvReflect),
                               FragmentShaderSpvReflect,
                               SPV_REFLECT_MODULE_FLAG_NO_COPY);
  throwIfFailed(fs.GetResult(),
                "SPIRV-Reflect: Failed to create shader module");
  return fs;
}

void reflect_descriptor_set_layouts(
    VulkanDevice &device, const spv_reflect::ShaderModule &vs,
    const spv_reflect::ShaderModule &fs,
    std::output_iterator<VkDescriptorSetLayout> auto out) {

  SmallVector<SpvReflectDescriptorSet *, 4> sets;
  Vector<SpvReflectDescriptorBinding *> bindings;

  struct SetInfo {
    LinearMap<unsigned, VkDescriptorSetLayoutBinding> bindings;
  };

  SmallLinearMap<unsigned, SetInfo, 4> set_infos;

  for (auto [shader, stage] : {std::pair(&vs, VK_SHADER_STAGE_VERTEX_BIT),
                               std::pair(&fs, VK_SHADER_STAGE_FRAGMENT_BIT)}) {
    uint32_t num_sets = 0;
    throwIfFailed(shader->EnumerateDescriptorSets(&num_sets, nullptr),
                  "SPIRV-Reflect: Failed to enumerate shader descriptor sets");
    sets.resize(num_sets);
    throwIfFailed(shader->EnumerateDescriptorSets(&num_sets, sets.data()),
                  "SPIRV-Reflect: Failed to enumerate shader descriptor sets");
    for (const auto *set : sets) {
      auto &set_info = set_infos[set->set];
      for (const auto *binding : std::span(set->bindings, set->binding_count)) {
        auto [it, inserted] = set_info.bindings.insert(binding->binding, {});
        auto &binding_info = std::get<1>(*it);
        binding_info.stageFlags |= stage;
        if (inserted) {
          binding_info.binding = binding->binding;
          binding_info.descriptorType =
              static_cast<VkDescriptorType>(binding->descriptor_type);
          binding_info.descriptorCount = binding->count;
        } else {
          assert(binding_info.binding == binding->binding);
          assert(binding_info.descriptorType ==
                 static_cast<VkDescriptorType>(binding->descriptor_type));
          assert(binding_info.descriptorCount == binding->count);
        }
      }
    }
  }

  for (const auto &[_, set_info] : set_infos) {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = unsigned(set_info.bindings.size()),
        .pBindings = set_info.bindings.data(),
    };
    VkDescriptorSetLayout layout;
    throwIfFailed(device.CreateDescriptorSetLayout(&create_info, &layout),
                  "Vulkan: Failed to create descriptor set layout");
    *out = layout;
  }
}

VkPipelineLayout
create_pipeline_layout(VulkanDevice &device,
                       std::span<const VkDescriptorSetLayout> set_layouts,
                       const VkPushConstantRange &pc_range) {
  VkPipelineLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = unsigned(set_layouts.size()),
      .pSetLayouts = set_layouts.data(),
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pc_range,
  };
  VkPipelineLayout layout;
  throwIfFailed(device.CreatePipelineLayout(&layout_info, &layout),
                "Vulkan: Failed to create pipeline layout");
  return layout;
}
} // namespace

auto VulkanPipelineCompiler::create(VulkanDevice &device)
    -> VulkanPipelineCompiler {
  auto vs = reflect_vs();
  auto fs = reflect_fs();

  SmallVector<VkDescriptorSetLayout, 4> set_layouts;
  reflect_descriptor_set_layouts(device, vs, fs,
                                 std::back_inserter(set_layouts));

  VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      .size = sizeof(ren::ModelData),
  };

  auto layout = create_pipeline_layout(device, set_layouts, pc_range);

  return VulkanPipelineCompiler(device, std::move(set_layouts), layout);
}

VulkanPipelineCompiler::VulkanPipelineCompiler(
    VulkanDevice &device, SmallVector<VkDescriptorSetLayout, 4> set_layouts,
    VkPipelineLayout layout)
    : MaterialPipelineCompiler(
          ".spv",
          PipelineSignature{
              .handle = AnyRef(layout,
                               [device = &device](VkPipelineLayout layout) {
                                 device->push_to_delete_queue(layout);
                               })}),
      m_device(&device), m_set_layouts(std::move(set_layouts)) {}

VulkanPipelineCompiler::VulkanPipelineCompiler(VulkanDevice &device)
    : VulkanPipelineCompiler(create(device)) {}

void VulkanPipelineCompiler::destroy() {
  for (auto set : m_set_layouts) {
    m_device->DestroyDescriptorSetLayout(set);
  }
}

VulkanPipelineCompiler::~VulkanPipelineCompiler() { destroy(); }
} // namespace ren
