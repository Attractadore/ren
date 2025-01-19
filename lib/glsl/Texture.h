#ifndef REN_GLSL_TEXTURE_H
#define REN_GLSL_TEXTURE_H

#include "DevicePtr.h"
#include "Std.h"

#if !GL_core_profile
#include <boost/predef/compiler.h>
#include <boost/preprocessor.hpp>
#endif

GLSL_NAMESPACE_BEGIN

#define TEXTURE_ID_TYPE uint32_t
const uint TEXTURE_ID_SIZE = 4;
static_assert(TEXTURE_ID_SIZE == sizeof(TEXTURE_ID_TYPE));

#define SAMPLER_ID_TYPE uint32_t
const uint SAMPLER_ID_SIZE = 4;
static_assert(SAMPLER_ID_SIZE == sizeof(SAMPLER_ID_TYPE));

// FIXME: minimum required maxPerStageUpdateAfterBindResources value required by
// Vulkan spec. Fetch dynamically based on device and clamp to some value
// instead.
const uint MAX_NUM_RESOURCES = 500 * 1000;
const uint NUM_SAMPLERS = 2048;

const uint SRV_SET = 0;
const uint CIS_SET = 1;
const uint UAV_SET = 2;
const uint SAMPLER_SET = 3;

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

// clang-format off

#if BOOST_COMP_MSVC
#pragma warning(push)
#pragma warning(disable: 4003)
#endif

DEFINE_TEXTURE_DESCRIPTOR(Texture, , ); 

DEFINE_TEXTURE_DESCRIPTOR(Texture2D, (Texture), );
GLSL_DEFINE_PTR_TYPE(Texture2D, TEXTURE_ID_SIZE);

DEFINE_TEXTURE_DESCRIPTOR(SampledTexture, , ); 

DEFINE_TEXTURE_DESCRIPTOR(SampledTexture2D, (SampledTexture), );
GLSL_DEFINE_PTR_TYPE(SampledTexture2D, TEXTURE_ID_SIZE);

DEFINE_TEXTURE_DESCRIPTOR(StorageTexture, , ); 

DEFINE_TEXTURE_DESCRIPTOR(StorageTexture2D, (StorageTexture), ); 
GLSL_DEFINE_PTR_TYPE(StorageTexture2D, TEXTURE_ID_SIZE);

DEFINE_TEXTURE_DESCRIPTOR(CoherentStorageTexture2D, (StorageTexture)(StorageTexture2D),);
GLSL_DEFINE_PTR_TYPE(CoherentStorageTexture2D, TEXTURE_ID_SIZE);

#if BOOST_COMP_MSVC
#pragma warning(pop)
#endif

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
