#pragma once
#include "ren/core/StdDef.hpp"

#define REN_NEW_TYPE(NewType, BaseType)                                        \
  class NewType {                                                              \
    BaseType m_value = {};                                                     \
                                                                               \
  public:                                                                      \
    constexpr NewType() = default;                                             \
    constexpr explicit NewType(BaseType value) noexcept                        \
        : m_value(std::move(value)) {}                                         \
                                                                               \
    operator BaseType() const noexcept { return m_value; }                     \
  };

#define REN_NEW_TEMPLATE_TYPE(NewType, BaseType, T)                            \
  template <typename T> class NewType {                                        \
    BaseType m_value = {};                                                     \
                                                                               \
  public:                                                                      \
    NewType() = default;                                                       \
    explicit NewType(BaseType value) noexcept : m_value(std::move(value)) {}   \
                                                                               \
    operator BaseType() const noexcept { return m_value; }                     \
                                                                               \
    explicit operator bool() const { return bool(m_value); }                   \
  };
