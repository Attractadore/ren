#include "Vulkan/VulkanReflection.hpp"
#include "Support/Views.hpp"
#include "Vulkan/VulkanDescriptors.hpp"
#include "Vulkan/VulkanErrors.hpp"
#include "Vulkan/VulkanFormats.hpp"
#include "Vulkan/VulkanShaderStages.hpp"

#include <range/v3/algorithm.hpp>

namespace ren {

namespace {

void parse_user_types(std::span<const uint32_t> data,
                      HashMap<unsigned, std::string_view> &user_types) {
  size_t index = 5;
  while (index < data.size()) {
    auto word_count = data[index] >> SpvWordCountShift;
    assert(word_count > 0);
    auto opcode = static_cast<SpvOp>(data[index] & SpvOpCodeMask);
    switch (opcode) {
    default:
      break;
    case SpvOpDecorateString:
    case SpvOpMemberDecorateString: {
      auto member_offset = (opcode == SpvOpDecorateString) ? 0 : 1;
      auto decoration = data[index + 2 + member_offset];
      if (decoration == SpvDecorationUserTypeGOOGLE) {
        auto id = data[index + 1];
        user_types[id] =
            reinterpret_cast<const char *>(&data[index + 3 + member_offset]);
      }
    } break;
    }
    index += word_count;
  }
}

void collect_input_variables(const spv_reflect::ShaderModule &module,
                             Vector<VertexAttribute> &out) {
  uint32_t num_input_vars = 0;
  throwIfFailed(module.EnumerateInputVariables(&num_input_vars, nullptr),
                "SPIRV-Reflect: Failed to enumerate shader input variables");
  out.reserve(num_input_vars);
  SmallVector<SpvReflectInterfaceVariable *, 32> input_vars(num_input_vars);
  throwIfFailed(
      module.EnumerateInputVariables(&num_input_vars, input_vars.data()),
      "SPIRV-Reflect: Failed to enumerate shader input variables");
  ranges::transform(
      input_vars | filter([](const SpvReflectInterfaceVariable *var) {
        return var->location != -1;
      }),
      std::back_inserter(out), [&](const SpvReflectInterfaceVariable *var) {
        auto count = [&] {
          switch (var->type_description->op) {
          default:
            return 1u;
          case SpvOpTypeMatrix:
            // This is a bit weird...
            // Matrices used as input variables are always packed as row-major
            // by both dxc and glslc in HLSL mode.
            // Obviously, column-major mode is preferable
            todo();
          }
        }();
        return VertexAttribute{
            .semantic = var->semantic ? var->semantic : "",
            .location = var->location,
            .count = count,
            .format = getFormat(static_cast<VkFormat>(var->format)),
        };
      });
}
} // namespace

VulkanReflectionModule::VulkanReflectionModule(std::span<const std::byte> data)
    : m_module([&] {
        spv_reflect::ShaderModule module(data.size_bytes(), data.data());
        throwIfFailed(module.GetResult(),
                      "SPIRV-Reflect: Failed to create shader module");
        return module;
      }()) {
  parse_user_types(
      {m_module.GetCode(), m_module.GetCodeSize() / sizeof(uint32_t)},
      m_user_types);
  collect_input_variables(m_module, m_input_variables);
}

auto VulkanReflectionModule::get_shader_stage() const -> ShaderStage {
  return getShaderStage(
      static_cast<VkShaderStageFlagBits>(m_module.GetShaderStage()));
}

auto VulkanReflectionModule::get_binding_count() const -> unsigned {
  uint32_t num_bindings = 0;
  throwIfFailed(m_module.EnumerateDescriptorBindings(&num_bindings, nullptr),
                "SPIRV-Reflect: Failed to enumerate shader bindings");
  return num_bindings;
}

namespace {
DescriptorType getDescriptorType(SpvReflectDescriptorType type,
                                 std::string_view user_type) {
  switch (type) {
  default:
    todo(fmt::format("Unsupported SpvReflectDescriptorType {}!", int(type)));
  case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
    return DESCRIPTOR_TYPE_SAMPLER;
  case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    return DESCRIPTOR_TYPE_TEXTURE;
  case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    return DESCRIPTOR_TYPE_RW_TEXTURE;
  case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    return DESCRIPTOR_TYPE_TEXEL_BUFFER;
  case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
    return DESCRIPTOR_TYPE_RW_TEXEL_BUFFER;
  case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    return DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
    if (user_type.starts_with("structuredbuffer")) {
      return DESCRIPTOR_TYPE_STRUCTURED_BUFFER;
    } else if (user_type.starts_with("rwstructuredbuffer") or
               user_type.starts_with("appendstructuredbuffer") or
               user_type.starts_with("consumestructuredbuffer")) {
      return DESCRIPTOR_TYPE_RW_STRUCTURED_BUFFER;
    } else if (user_type == "byteaddressbuffer") {
      return DESCRIPTOR_TYPE_RAW_BUFFER;
    } else if (user_type == "rwbyteaddressbuffer") {
      return DESCRIPTOR_TYPE_RW_RAW_BUFFER;
    }
    unreachable("Unknown UserTypeGOOGLE {}", user_type);
  }
  }
}
} // namespace

void VulkanReflectionModule::get_bindings(
    std::span<DescriptorBindingReflection> out) const {
  auto num_bindings = get_binding_count();
  assert(out.size() >= num_bindings);
  SmallVector<SpvReflectDescriptorBinding *> bindings(num_bindings);
  throwIfFailed(
      m_module.EnumerateDescriptorBindings(&num_bindings, bindings.data()),
      "SPIRV-Reflect: Failed to enumerate shader bindings");
  ranges::transform(
      bindings, out.data(), [&](const SpvReflectDescriptorBinding *binding) {
        assert(binding);
        auto user_type = m_user_types.find(binding->spirv_id)->second;
        return DescriptorBindingReflection{
            .set = binding->set,
            .binding = {
                .binding = binding->binding,
                .type = getDescriptorType(binding->descriptor_type, user_type),
                .count = binding->count,
                .stages = get_shader_stage(),
            }};
      });
}

auto VulkanReflectionModule::get_input_variable_count() const -> unsigned {
  return m_input_variables.size();
}

void VulkanReflectionModule::get_input_variables(
    std::span<VertexAttribute> out) const {
  assert(out.size() >= m_input_variables.size());
  ranges::copy(m_input_variables, out.data());
}

} // namespace ren
