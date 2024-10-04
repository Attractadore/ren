#ifndef REN_GLSL_TEXTURE_H
#define REN_GLSL_TEXTURE_H

#include "Common.h"
#include "DevicePtr.h"

#if !GL_core_profile
#include <boost/preprocessor.hpp>
#include <limits>
#endif

GLSL_NAMESPACE_BEGIN

#define SAMPLER_ID_TYPE uint32_t
const uint SAMPLER_ID_SIZE = 4;
static_assert(SAMPLER_ID_SIZE == sizeof(SAMPLER_ID_TYPE));

const uint NUM_SAMPLERS = 1 << 16;
static_assert(NUM_SAMPLERS - 1 <= std::numeric_limits<SAMPLER_ID_TYPE>::max());

#define TEXTURE_ID_TYPE uint32_t
const uint TEXTURE_ID_SIZE = 4;
static_assert(TEXTURE_ID_SIZE == sizeof(TEXTURE_ID_TYPE));

const uint NUM_TEXTURES = 1 << 16;
const uint NUM_SAMPLED_TEXTURES = 1 << 16;
const uint NUM_STORAGE_TEXTURES = 1 << 16;

static_assert(NUM_TEXTURES - 1 <= std::numeric_limits<TEXTURE_ID_TYPE>::max());
static_assert(NUM_SAMPLED_TEXTURES - 1 <=
              std::numeric_limits<TEXTURE_ID_TYPE>::max());
static_assert(NUM_STORAGE_TEXTURES - 1 <=
              std::numeric_limits<TEXTURE_ID_TYPE>::max());

const uint SAMPLERS_SLOT = 0;
const uint TEXTURES_SLOT = 1;
const uint SAMPLED_TEXTURES_SLOT = 2;
const uint STORAGE_TEXTURES_SLOT = 3;

#if GL_core_profile

struct SamplerState {
  SAMPLER_ID_TYPE id;
};

#else

class SamplerState {
public:
  SamplerState() = default;
  explicit SamplerState(TEXTURE_ID_TYPE id) { m_id = id; }

  template <std::integral I> explicit operator I() const { return m_id; }

  explicit operator bool() const { return m_id != 0; }

  bool operator==(const SamplerState &) const = default;

private:
  SAMPLER_ID_TYPE m_id = 0;
};

#endif

#if GL_core_profile

#define DEFINE_TEXTURE_DESCRIPTOR(Type, FromTypes, ToTypes)                    \
  struct Type {                                                                \
    SAMPLER_ID_TYPE id;                                                        \
  }

#else

#define DEFINE_TEXTURE_DESCRIPTOR_EXPLICIT_CONSTRUCTOR(r, Type, FromType)      \
public:                                                                        \
  explicit Type(FromType id) { m_id = TEXTURE_ID_TYPE(id); }

#define DEFINE_TEXTURE_DESCRIPTOR_IMPLICIT_CONVERSION(r, data, ToType)         \
public:                                                                        \
  operator ToType() const { return ToType(m_id); }

#define DEFINE_TEXTURE_DESCRIPTOR_EXPLICIT_CONSTRUCTORS(Type, FromTypes)       \
  BOOST_PP_SEQ_FOR_EACH(DEFINE_TEXTURE_DESCRIPTOR_EXPLICIT_CONSTRUCTOR, Type,  \
                        FromTypes)

#define DEFINE_TEXTURE_DESCRIPTOR_IMPLICIT_CONVERSIONS(FromTypes)              \
  BOOST_PP_SEQ_FOR_EACH(DEFINE_TEXTURE_DESCRIPTOR_IMPLICIT_CONVERSION, _,      \
                        FromTypes)

#define DEFINE_TEXTURE_DESCRIPTOR(Type, FromTypes, ToTypes)                    \
  class Type {                                                                 \
  public:                                                                      \
    Type() = default;                                                          \
    explicit Type(TEXTURE_ID_TYPE id) { m_id = id; }                           \
                                                                               \
    template <std::integral I> explicit operator I() const { return m_id; }    \
                                                                               \
    explicit operator bool() const { return m_id != 0; }                       \
                                                                               \
    bool operator==(const Type &) const = default;                             \
                                                                               \
  private:                                                                     \
    TEXTURE_ID_TYPE m_id = 0;                                                  \
                                                                               \
  public:                                                                      \
    DEFINE_TEXTURE_DESCRIPTOR_EXPLICIT_CONSTRUCTORS(Type, FromTypes)           \
    DEFINE_TEXTURE_DESCRIPTOR_IMPLICIT_CONVERSIONS(FromTypes)                  \
    DEFINE_TEXTURE_DESCRIPTOR_IMPLICIT_CONVERSIONS(ToTypes)                    \
  }

#endif

GLSL_DEFINE_PTR_TYPE(SamplerState, SAMPLER_ID_SIZE);

const SamplerState DEFAULT_SAMPLER = SamplerState(SAMPLER_ID_TYPE(1));

// clang-format off
//
DEFINE_TEXTURE_DESCRIPTOR(Texture, , ); 

DEFINE_TEXTURE_DESCRIPTOR(Texture2D, (Texture), );
GLSL_DEFINE_PTR_TYPE(Texture2D, TEXTURE_ID_SIZE);

DEFINE_TEXTURE_DESCRIPTOR(SampledTexture, , ); 

DEFINE_TEXTURE_DESCRIPTOR(SampledTexture2D, (SampledTexture), );
GLSL_DEFINE_PTR_TYPE(SampledTexture2D, TEXTURE_ID_SIZE);

DEFINE_TEXTURE_DESCRIPTOR(StorageTexture, , ); 
DEFINE_TEXTURE_DESCRIPTOR(RWStorageTexture, , (StorageTexture)); 

DEFINE_TEXTURE_DESCRIPTOR(StorageTexture2D, (StorageTexture), ); 
GLSL_DEFINE_PTR_TYPE(StorageTexture2D, TEXTURE_ID_SIZE);
DEFINE_TEXTURE_DESCRIPTOR(RWStorageTexture2D, (RWStorageTexture), (StorageTexture)(StorageTexture2D)); 
GLSL_DEFINE_PTR_TYPE(RWStorageTexture2D, TEXTURE_ID_SIZE);

DEFINE_TEXTURE_DESCRIPTOR(CoherentRWStorageTexture2D, (RWStorageTexture)(RWStorageTexture2D), (StorageTexture)(StorageTexture2D));
GLSL_DEFINE_PTR_TYPE(CoherentRWStorageTexture2D, TEXTURE_ID_SIZE);

// clang-format on

#undef DEFINE_TEXTURE_DESCRIPTOR_IMPLICIT_CONVERSION
#undef DEFINE_TEXTURE_DESCRIPTOR_IMPLICIT_CONVERSIONS
#undef DEFINE_TEXTURE_DESCRIPTOR_EXPLICIT_CONSTRUCTOR
#undef DEFINE_TEXTURE_DESCRIPTOR_EXPLICIT_CONSTRUCTORS
#undef DEFINE_TEXTURE_DESCRIPTOR

#undef SAMPLER_ID_TYPE
#undef TEXTURE_ID_TYPE

GLSL_NAMESPACE_END

#endif // REN_GLSL_TEXTURE_H
