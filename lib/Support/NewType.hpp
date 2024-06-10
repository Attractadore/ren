#pragma once
#include "Support/Hash.hpp"

#define REN_NEW_TYPE(Name, Type)                                               \
  class Name {                                                                 \
    Type value = {};                                                           \
                                                                               \
  public:                                                                      \
    Name() = default;                                                          \
    explicit Name(Type value) noexcept : value(std::move(value)) {}            \
                                                                               \
    operator Type() const noexcept { return value; }                           \
  };                                                                           \
                                                                               \
  template <> struct Hash<Name> {                                              \
    auto operator()(const Name &value) const noexcept -> std::size_t {         \
      return Hash<Type>()(value);                                              \
    }                                                                          \
  }

#define REN_NEW_TEMPLATE_TYPE(Name, Type, T)                                   \
  template <typename T> class Name {                                           \
    Type value = {};                                                           \
                                                                               \
  public:                                                                      \
    Name() = default;                                                          \
    explicit Name(Type value) noexcept : value(std::move(value)) {}            \
                                                                               \
    operator Type() const noexcept { return value; }                           \
  };                                                                           \
                                                                               \
  template <typename T> struct Hash<Name<T>> {                                 \
    auto operator()(const Name<T> &value) const noexcept -> std::size_t {      \
      return Hash<Type>()(value);                                              \
    }                                                                          \
  }
