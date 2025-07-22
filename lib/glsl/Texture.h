#pragma once
#if !SLANG
#include "DevicePtr.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

#define TextureId uint32_t
const uint TEXTURE_ID_SIZE = 4;
static_assert(TEXTURE_ID_SIZE == sizeof(TextureId));

#define SamplerId uint32_t
const uint SAMPLER_ID_SIZE = 4;
static_assert(SAMPLER_ID_SIZE == sizeof(SamplerId));

// TODO: minimum maxPerStageUpdateAfterBindResources value required by
// Vulkan spec is 500 000. Fetch dynamically based on device and clamp to some
// value instead.
const uint MAX_NUM_RESOURCES = 100 * 1000;
const uint MAX_NUM_SAMPLERS = 2048;

const uint SAMPLER_SLOT = 0;
const uint SRV_SLOT = 1;
const uint CIS_SLOT = 2;
const uint UAV_SLOT = 3;

#if GL_core_profile

struct SamplerState {
  SamplerId id;
};

#else

class SamplerState {
public:
  SamplerState() = default;
  explicit SamplerState(SamplerId id) { m_id = id; }

  template <std::integral I> explicit operator I() const { return m_id; }

  explicit operator bool() const { return m_id != 0; }

  bool operator==(const SamplerState &) const = default;

private:
  SamplerId m_id = 0;
};

#endif

#if GL_core_profile

#define DEFINE_BASE_DESCRIPTOR(Type) SamplerId id;

#define DEFINE_DESCRIPTOR(Type, Base) DEFINE_BASE_DESCRIPTOR(Type)

#else

#define DEFINE_BASE_DESCRIPTOR(Type)                                           \
public:                                                                        \
  Type() = default;                                                            \
  explicit Type(TextureId id) { m_id = id; }                                   \
                                                                               \
  template <std::integral I> explicit operator I() const { return m_id; }      \
                                                                               \
  explicit operator bool() const { return m_id != 0; }                         \
                                                                               \
  bool operator==(const Type &) const = default;                               \
                                                                               \
private:                                                                       \
  TextureId m_id = 0;

#define DEFINE_DESCRIPTOR(Type, Base)                                          \
  DEFINE_BASE_DESCRIPTOR(Type)                                                 \
public:                                                                        \
  explicit Type(Base id) { m_id = TextureId(id); }                             \
  operator Base() const { return Base(m_id); }

#endif

GLSL_DEFINE_PTR_TYPE(SamplerState, SAMPLER_ID_SIZE);

struct Texture {
  DEFINE_BASE_DESCRIPTOR(Texture)
};

struct Texture2D {
  DEFINE_DESCRIPTOR(Texture2D, Texture)
};
GLSL_DEFINE_PTR_TYPE(Texture2D, TEXTURE_ID_SIZE);

struct TextureCube {
  DEFINE_DESCRIPTOR(TextureCube, Texture)
};
GLSL_DEFINE_PTR_TYPE(TextureCube, TEXTURE_ID_SIZE);

struct Texture3D {
  DEFINE_DESCRIPTOR(Texture3D, Texture)
};
GLSL_DEFINE_PTR_TYPE(Texture3D, TEXTURE_ID_SIZE);

struct SampledTexture {
  DEFINE_BASE_DESCRIPTOR(SampledTexture)
};

struct SampledTexture2D {
  DEFINE_DESCRIPTOR(SampledTexture2D, SampledTexture)
};
GLSL_DEFINE_PTR_TYPE(SampledTexture2D, TEXTURE_ID_SIZE);

struct SampledTexture2DArray {
  DEFINE_DESCRIPTOR(SampledTexture2DArray, SampledTexture)
};
GLSL_DEFINE_PTR_TYPE(SampledTexture2DArray, TEXTURE_ID_SIZE);

struct SampledTextureCube {
  DEFINE_DESCRIPTOR(SampledTextureCube, SampledTexture)
};
GLSL_DEFINE_PTR_TYPE(SampledTextureCube, TEXTURE_ID_SIZE);

struct SampledTexture3D {
  DEFINE_DESCRIPTOR(SampledTexture3D, SampledTexture)
};
GLSL_DEFINE_PTR_TYPE(SampledTexture3D, TEXTURE_ID_SIZE);

struct StorageTexture {
  DEFINE_BASE_DESCRIPTOR(StorageTexture)
};

struct StorageTexture2D {
  DEFINE_DESCRIPTOR(StorageTexture2D, StorageTexture)
};
GLSL_DEFINE_PTR_TYPE(StorageTexture2D, TEXTURE_ID_SIZE);

struct StorageTextureCube {
  DEFINE_DESCRIPTOR(StorageTextureCube, StorageTexture)
};
GLSL_DEFINE_PTR_TYPE(StorageTextureCube, TEXTURE_ID_SIZE);

struct StorageTexture3D {
  DEFINE_DESCRIPTOR(StorageTexture3D, StorageTexture)
};
GLSL_DEFINE_PTR_TYPE(StorageTexture3D, TEXTURE_ID_SIZE);

#undef DEFINE_BASE_DESCRIPTOR
#undef DEFINE_DESCRIPTOR

#undef TextureId
#undef SamplerId

const SamplerState SAMPLER_NEAREST_CLAMP = SamplerState(1);
const SamplerState SAMPLER_LINEAR_MIP_NEAREST_CLAMP = SamplerState(2);

GLSL_NAMESPACE_END
#endif
