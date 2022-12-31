#include "interface.hlsl"

namespace ren {
#if REN_HLSL_VULKAN
#define PUSH_CONSTANTS(type, name)                                             \
  cbuffer PushConstants { [[vk::push_constant]] type name; }
#elif REN_HLSL_DIRECTX12
#define PUSH_CONSTANTS(type, name)                                             \
  cbuffer PushConstants : REGISTER(b, REN_DIRECTX12_PUSH_CONSTANTS_CB) {       \
    type name;                                                                 \
  }
#endif

#if REN_HLSL_VULKAN
template <typename T> T ptr_load(uint64_t base, uint idx) {
  return vk::RawBufferLoad(base + idx * sizeof(T));
}
#endif
} // namespace ren
