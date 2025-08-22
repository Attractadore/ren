#pragma once

#if __cplusplus

#include "../DevicePtr.hpp"
#include "../core/Assert.hpp"
#include "../core/StdDef.hpp"

#include <array>
#include <concepts>
#include <cstdint>
#include <glm/glm.hpp>

#define SH_IN(T) const T &
#define SH_OUT(T) T &
#define SH_INOUT(T) T &

#define SH_ARRAY(T, name, SIZE) std::array<T, SIZE> name

namespace ren {

template <typename T> struct RgIgnore {
  RgIgnore() = default;
  RgIgnore(T value) { m_value = std::move(value); }

  T m_value;
};

} // namespace ren

#define SH_RG_IGNORE(T) RgIgnore<T>

namespace ren::sh {
using namespace glm;
}

#endif

#if __SLANG__

import glsl;

#define inline
#define static_assert(expr)

#define SH_IN(T) T
#define SH_OUT(T) out T
#define SH_INOUT(T) inout T

#define SH_ARRAY(T, name, SIZE) T name[SIZE]

#define DevicePtr Ptr

enum class MemoryScope {
  Global = 1,
  Workgroup = 2,
  Subgroup = 3,
};

enum class MemoryOrder {
  Relaxed = 0,
  Acquire = 2,
  Release = 4,
  AcquireRelease = 8,
};

void global_buffer_barrier(MemoryOrder order) {
  uint arg = (uint)order | 0x40;
  spirv_asm{OpMemoryBarrier $MemoryScope::Global $arg};
}

void global_image_barrier(MemoryOrder order) {
  uint arg = (uint)order | 0x800;
  spirv_asm{OpMemoryBarrier $MemoryScope::Global $arg};
}

#define SH_RG_IGNORE(T) T

#endif

namespace ren::sh {

/// MATH ///

static const float PI = 3.1416f;
static const float TWO_PI = 6.2832f;

inline mat4 as_mat4(mat4x3 m) {
  return mat4(vec4(m[0], 0.0f), vec4(m[1], 0.0f), vec4(m[2], 0.0f),
              vec4(m[3], 1.0f));
}

#define sh_ceil_div(nom, denom) ((nom) / (denom) + uint((nom) % (denom) != 0))

inline uint ceil_div(uint nom, uint denom) { return sh_ceil_div(nom, denom); }

// https://www.desmos.com/calculator/lzzhuthh1g
inline float acos_0_to_1_fast(float x) {
  x = clamp(x, 0.0f, 1.0f);
  float taylor_0 = 0.5f * PI - x - x * x * x / 6.0f;
  float taylor_1 = sqrt(2.0f * (1.0f - x)) * (1.0f + (1.0f - x) / 12.0f);
  return mix(taylor_0, taylor_1, x);
}

inline float acos_fast(float x) {
  float r = acos_0_to_1_fast(abs(x));
  return x < 0.0f ? PI - r : r;
}

// https://old.reddit.com/r/vulkan/comments/c4r7qx/erf_for_vulkan/esnvdnf/
inline float erf_fast(float x) {
  float xa = abs(x);

  float y = xa * (xa * (xa * 0.0038004543f + 0.020338153f) + 0.03533611f) +
            1.0000062f;

  // y = y^32
  y = y * y;
  y = y * y;
  y = y * y;
  y = y * y;
  y = y * y;

  y = 1.0f - 1.0f / y;

  return x < 0.0f ? -y : y;
}

inline float erf_0_inf_fast(float x) {
  float y =
      x * (x * (x * 0.0038004543f + 0.020338153f) + 0.03533611f) + 1.0000062f;

  // y = y^32
  y = y * y;
  y = y * y;
  y = y * y;
  y = y * y;
  y = y * y;

  y = 1.0f - 1.0f / y;

  return y;
}

/// BUFFERS ///

static const uint DEFAULT_DEVICE_PTR_ALIGNMENT = 16;

static const uint DEVICE_CACHE_LINE_SIZE = 128;

/// TEXTURES ///

// TODO: minimum maxPerStageUpdateAfterBindResources value required by
// Vulkan spec is 500 000. Fetch dynamically based on device and clamp to some
// value instead.
static const uint MAX_NUM_RESOURCES = 100 * 1000;
static const uint MAX_NUM_SAMPLERS = 2048;

enum BindingSlot {
  SAMPLER_STATE_SLOT,
  TEXTURE_SLOT,
  SAMPLER_SLOT,
  RW_TEXTURE_SLOT,
};

#if __cplusplus

enum class DescriptorKind : u32 {
  Unknown,
  SamplerState,
  Texture,
  Sampler,
  RWTexture,
};

struct SamplerState;
struct Texture2D;
struct TextureCube;
struct Sampler2D;
struct SamplerCube;
struct Sampler3D;
struct RWTexture2D;
struct RWTexture2DArray;

namespace detail {

template <typename T> constexpr auto DescriptorKindImpl = nullptr;

#define define_opaque_descriptor(Type, Kind)                                   \
  template <>                                                                  \
  inline constexpr DescriptorKind DescriptorKindImpl<Type> =                   \
      DescriptorKind::Kind

define_opaque_descriptor(void, Unknown);
define_opaque_descriptor(SamplerState, SamplerState);
define_opaque_descriptor(Texture2D, Texture);
define_opaque_descriptor(TextureCube, Texture);
define_opaque_descriptor(Sampler2D, Sampler);
define_opaque_descriptor(SamplerCube, Sampler);
define_opaque_descriptor(Sampler3D, Sampler);
define_opaque_descriptor(RWTexture2D, RWTexture);
define_opaque_descriptor(RWTexture2DArray, RWTexture);

#undef define_opaque_descriptor

} // namespace detail

template <typename T = void> struct Handle {
  Handle() = default;
  explicit Handle(u32 id)
    requires(not std::same_as<T, void>)
  {
    m_id = id;
  }

  Handle(u32 id, DescriptorKind kind)
    requires std::same_as<T, void>
  {
    m_id = id;
    m_kind = kind;
  }

  static const DescriptorKind Kind = detail::DescriptorKindImpl<T>;

  explicit operator bool() const { return m_id != 0; }

  template <typename U>
    requires std::same_as<T, void>
  explicit operator Handle<U>() const {
    ren_assert(!m_id or m_kind == Handle<U>::Kind);
    return Handle<U>(m_id);
  }

  bool operator==(const Handle &) const = default;

  u32 m_id : 29 = 0;
  DescriptorKind m_kind : 3 = Kind;
};
static_assert(sizeof(Handle<void>) == sizeof(uint));

template <typename T, DescriptorKind K>
concept DescriptorOfKind = (detail::DescriptorKindImpl<T> == K);

template <typename T, DescriptorKind K>
concept HandleOfKind = (T::Kind == K);

#endif

#if __SLANG__

// clang-format off
[[vk::binding(SAMPLER_STATE_SLOT)]] uniform __DynamicResource<__DynamicResourceKind.Sampler> g_sampler_states[MAX_NUM_SAMPLERS];
[[vk::binding(TEXTURE_SLOT)]] uniform __DynamicResource<__DynamicResourceKind.General> g_textures[MAX_NUM_RESOURCES];
[[vk::binding(SAMPLER_SLOT)]] uniform __DynamicResource<__DynamicResourceKind.General> g_samplers[MAX_NUM_RESOURCES];
[[vk::binding(RW_TEXTURE_SLOT)]] uniform __DynamicResource<__DynamicResourceKind.General> g_rw_textures[MAX_NUM_RESOURCES];
[[vk::binding(RW_TEXTURE_SLOT)]] uniform globallycoherent __DynamicResource<__DynamicResourceKind.General> g_coherent_rw_textures[MAX_NUM_RESOURCES];
// clang-format on

struct Handle<T : IOpaqueDescriptor> {
  uint m_id : 29;
  uint m_kind : 3;
};

T Get<T : IOpaqueDescriptor>(Handle<T> handle) {
  uint id = handle.m_id;
  if (T.kind == DescriptorKind.Sampler) {
    return ren::sh::g_sampler_states[id].asOpaqueDescriptor<T>();
  } else if (T.kind == DescriptorKind.CombinedTextureSampler) {
    return ren::sh::g_samplers[id].asOpaqueDescriptor<T>();
  } else if (T.kind == DescriptorKind.Texture &&
             T.descriptorAccess == DescriptorAccess.Read) {
    return ren::sh::g_textures[id].asOpaqueDescriptor<T>();
  }
  return ren::sh::g_rw_textures[id].asOpaqueDescriptor<T>();
}

bool IsNull<T : IOpaqueDescriptor>(Handle<T> handle) {
  return handle.m_id == 0;
}

void CoherentStore(Handle<RWTexture2D> handle, uvec2 pos, vec4 value) {
  g_coherent_rw_textures[handle.m_id].asOpaqueDescriptor<RWTexture2D>().Store(
      pos, value);
}

vec4 CoherentLoad(Handle<RWTexture2D> handle, uvec2 pos) {
  return g_coherent_rw_textures[handle.m_id]
      .asOpaqueDescriptor<RWTexture2D>()
      .Load(pos);
}

uvec2 TextureSize(Sampler2D texture) {
  uvec2 size;
  texture.GetDimensions(size.x, size.y);
  return size;
}

uvec2 TextureSize(Texture2D texture) {
  uvec2 size;
  texture.GetDimensions(size.x, size.y);
  return size;
}

uvec2 TextureSize(TextureCube texture) {
  uvec2 size;
  texture.GetDimensions(size.x, size.y);
  return size;
}

uvec2 TextureSize(RWTexture2D texture) {
  uvec2 size;
  texture.GetDimensions(size.x, size.y);
  return size;
}

uvec3 TextureSize(RWTexture2DArray texture) {
  uvec3 size;
  texture.GetDimensions(size.x, size.y, size.z);
  return size;
}

uint TextureMips(Sampler2D texture) {
  uvec2 size;
  uint num_mips;
  texture.GetDimensions(0, size.x, size.y, num_mips);
  return num_mips;
}

uint TextureMips(Texture2D texture) {
  uvec2 size;
  uint num_mips;
  texture.GetDimensions(0, size.x, size.y, num_mips);
  return num_mips;
}

uint TextureMips(SamplerCube texture) {
  uvec2 size;
  uint num_mips;
  texture.GetDimensions(0, size.x, size.y, num_mips);
  return num_mips;
}

uint TextureMips(TextureCube texture) {
  uvec2 size;
  uint num_mips;
  texture.GetDimensions(0, size.x, size.y, num_mips);
  return num_mips;
}

#endif

/// DRAW INDIRECT COMMANDS ///

struct DrawIndirectCommand {
  uint32_t num_vertices;
  uint32_t num_instances;
  uint32_t base_vertex;
  uint32_t base_instance;
};

struct DrawIndexedIndirectCommand {
  uint32_t num_indices;
  uint32_t num_instances;
  uint32_t base_index;
  uint32_t base_vertex;
  uint32_t base_instance;
};

struct DispatchIndirectCommand {
  uint32_t x;
  uint32_t y;
  uint32_t z;
};

} // namespace ren::sh
