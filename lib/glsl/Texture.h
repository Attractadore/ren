#ifndef REN_GLSL_TEXTURE_H
#define REN_GLSL_TEXTURE_H

#include "DevicePtr.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

#define TextureId uint32_t
const uint TEXTURE_ID_SIZE = 4;
static_assert(TEXTURE_ID_SIZE == sizeof(TextureId));

#define SamplerId uint32_t
const uint SAMPLER_ID_SIZE = 4;
static_assert(SAMPLER_ID_SIZE == sizeof(SamplerId));

// FIXME: minimum maxPerStageUpdateAfterBindResources value required by
// Vulkan spec. Fetch dynamically based on device and clamp to some value
// instead.
const uint MAX_NUM_RESOURCES = 500 * 1000;
const uint MAX_NUM_SAMPLERS = 2048;

const uint SRV_SET = 0;
const uint CIS_SET = 1;
const uint UAV_SET = 2;
const uint SAMPLER_SET = 3;
const uint BUFFER_SET = 4;

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

struct SampledTexture {
  DEFINE_BASE_DESCRIPTOR(SampledTexture)
};

struct SampledTexture2D {
  DEFINE_DESCRIPTOR(SampledTexture2D, SampledTexture)
};
GLSL_DEFINE_PTR_TYPE(SampledTexture2D, TEXTURE_ID_SIZE);

struct StorageTexture {
  DEFINE_BASE_DESCRIPTOR(StorageTexture)
};

struct StorageTexture2D {
  DEFINE_DESCRIPTOR(StorageTexture2D, StorageTexture)
};
GLSL_DEFINE_PTR_TYPE(StorageTexture2D, TEXTURE_ID_SIZE);

struct CoherentStorageTexture2D {
  DEFINE_DESCRIPTOR(CoherentStorageTexture2D, StorageTexture2D)
};
GLSL_DEFINE_PTR_TYPE(CoherentStorageTexture2D, TEXTURE_ID_SIZE);

#undef DEFINE_BASE_DESCRIPTOR
#undef DEFINE_DESCRIPTOR

#undef TextureId
#undef SamplerId

GLSL_NAMESPACE_END

#endif // REN_GLSL_TEXTURE_H
