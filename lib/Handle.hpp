#pragma once
#include "Support/Hash.hpp"
#include "Support/SlotMapKey.hpp"

namespace ren {

template <typename H> REN_DEFINE_SLOTMAP_KEY(Handle);

template <typename H> struct Hash<Handle<H>> {
  auto operator()(Handle<H> handle) const -> std::size_t {
    auto value = std::bit_cast<unsigned>(handle);
    return Hash<unsigned>()(value);
  }
};

namespace detail {
struct NullHandleImpl {
  template <typename H> operator Handle<H>() const { return {}; }
};
} // namespace detail

constexpr inline detail::NullHandleImpl NullHandle;

template <typename H> class AutoHandle {
public:
  AutoHandle() = default;

  explicit AutoHandle(Handle<H> handle) { m_handle = handle; }

  AutoHandle(decltype(NullHandle)) {}

  AutoHandle(const AutoHandle &other) = delete;

  AutoHandle(AutoHandle &&other) noexcept {
    m_handle = std::exchange(other.m_handle, NullHandle);
  }

  ~AutoHandle() { destroy(); }

  AutoHandle &operator=(const AutoHandle &other) = delete;

  AutoHandle &operator=(AutoHandle &&other) noexcept {
    destroy();
    m_handle = other.m_handle;
    other.m_handle = NullHandle;
    return *this;
  }

  AutoHandle &operator=(decltype(NullHandle)) {
    reset();
    return *this;
  }

  auto get() const -> Handle<H> { return m_handle; }

  operator Handle<H>() const & { return m_handle; }

  operator Handle<H>() && = delete;

  void reset() {
    destroy();
    m_handle = NullHandle;
  }

  explicit operator bool() const { return m_handle != NullHandle; }

  auto release() -> Handle<H> { return std::exchange(m_handle, NullHandle); }

private:
  void destroy();

private:
  friend class Renderer;
  Handle<H> m_handle;
};

} // namespace ren
