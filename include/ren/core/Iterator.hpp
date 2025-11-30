#pragma once
#include <concepts>

namespace ren {

// https://www.boost.org/doc/libs/1_87_0/libs/iterator/doc/iterator_facade.html
// https://vector-of-bool.github.io/2020/06/13/cpp20-iter-facade.html

namespace detail {

template <typename Iter> struct DeduceIterValueType {
  static const Iter &it;
  using type = std::remove_cvref_t<decltype(*it)>;
};

template <typename Iter>
  requires requires { typename Iter::value_type; }
struct DeduceIterValueType<Iter> {
  using type = Iter::value_type;
};

template <typename Iter>
using DeduceIterValueTypeT = DeduceIterValueType<Iter>::type;

template <typename Iter>
concept CanRead = requires(const Iter &it) {
  { it.dereference() } -> std::convertible_to<DeduceIterValueTypeT<Iter>>;
};

template <typename Iter>
concept CanWrite = requires(const Iter &it) {
  { it.dereference() } -> std::assignable_from<DeduceIterValueType<Iter>>;
};

template <typename Iter>
concept CanCompare = requires(const Iter &it) {
  { it.equal(it) } -> std::convertible_to<bool>;
};

template <typename Iter>
concept CanIncrement = requires(Iter &it) { it.increment(); };

template <typename Iter>
concept CanDecrement = requires(Iter &it) { it.decrement(); };

template <typename Iter>
concept CanAdvance = requires(Iter &it) {
  requires requires(std::ptrdiff_t n) { it.advance(n); };
};

template <typename Iter>
concept CanDistanceTo = requires(const Iter &it) {
  { it.distance_to(it) } -> std::convertible_to<std::ptrdiff_t>;
};

template <typename Iter>
concept IsInputIterator = CanRead<Iter> and CanCompare<Iter>;

template <typename Iter>
concept IsForwardIterator = IsInputIterator<Iter> and CanIncrement<Iter>;

template <typename Iter>
concept IsBidirectionalIterator =
    IsForwardIterator<Iter> and CanDecrement<Iter>;

template <typename Iter>
concept IsRandomAccessIterator =
    IsBidirectionalIterator<Iter> and CanAdvance<Iter> and CanDistanceTo<Iter>;

template <typename Iter>
concept IsOutputIterator = CanWrite<Iter>;

} // namespace detail

template <typename Self> struct IteratorFacade {
  Self &self() { return *static_cast<Self *>(this); }

  const Self &self() const { return *static_cast<const Self *>(this); }

  bool equal(const Self &other) const
    requires detail::CanDistanceTo<Self>
  {
    return self().distance_to(other) == 0;
  }

  void increment() { self().advance(1); }

  void decrement() { self().advance(-1); }

  decltype(auto) operator*() const
    requires detail::CanRead<Self>
  {
    return self().dereference();
  }

  auto *operator->() const
    requires requires(Self &it) {
      {
        it.dereference()
      } -> std::convertible_to<const detail::DeduceIterValueTypeT<Self> &>;
    }
  {
    return &self().dereference();
  }

  decltype(auto) operator[](std::ptrdiff_t index) { return *(self() + index); }

  Self &operator++()
    requires detail::CanIncrement<Self>
  {
    self().increment();
    return self();
  }

  Self operator++(int)
    requires detail::CanIncrement<Self>
  {
    Self old = self();
    self().increment();
    return old;
  }

  Self &operator--()
    requires detail::CanDecrement<Self>
  {
    self().decrement();
    return self();
  }

  Self operator--(int)
    requires detail::CanDecrement<Self>
  {
    Self tmp = self();
    self().decrement();
    return tmp;
  }

  Self &operator+=(std::ptrdiff_t n)
    requires detail::CanAdvance<Self>
  {
    self().advance(n);
    return self();
  }

  Self &operator-=(std::ptrdiff_t n)
    requires detail::CanAdvance<Self>
  {
    self().advance(-n);
    return self();
  }

  Self operator+(std::ptrdiff_t n) const {
    Self tmp = self();
    return tmp += n;
  }

  Self operator-(std::ptrdiff_t n) const {
    Self tmp = self();
    return tmp -= n;
  }

  std::ptrdiff_t operator-(const Self &other) const
    requires detail::CanDistanceTo<Self>
  {
    return other.distance_to(self());
  }

  auto operator<=>(const Self &other) const
    requires detail::CanDistanceTo<Self>
  {
    return self().distance_to(other) <=> 0;
  }
};

template <typename Self>
bool operator==(const IteratorFacade<Self> &lhs,
                const IteratorFacade<Self> &rhs)
  requires detail::CanCompare<Self>
{
  return lhs.self().equal(rhs.self());
}

} // namespace ren

namespace std {

template <class Iter> struct iterator_traits;

struct output_iterator_tag;
struct input_iterator_tag;
struct forward_iterator_tag;
struct bidirectional_iterator_tag;
struct random_access_iterator_tag;

} // namespace std

template <class Iter>
  requires std::derived_from<Iter, ren::IteratorFacade<Iter>>
struct std::iterator_traits<Iter> {
  static const Iter &it;
  using difference_type = std::ptrdiff_t;
  using reference = decltype(*it);
  using pointer = decltype(it.operator->());
  using value_type = ren::detail::DeduceIterValueTypeT<Iter>;
  // clang-format off
  using iterator_category = std::conditional_t<
    ren::detail::IsRandomAccessIterator<Iter>, std::random_access_iterator_tag, std::conditional_t<
    ren::detail::IsBidirectionalIterator<Iter>, std::bidirectional_iterator_tag, std::conditional_t<
    ren::detail::IsForwardIterator<Iter>, std::forward_iterator_tag, std::conditional_t<
    ren::detail::IsInputIterator<Iter>, std::input_iterator_tag, std::conditional_t<
    ren::detail::IsOutputIterator<Iter>, std::output_iterator_tag, void>>>>>;
  // clang-format on
  using iterator_concept = iterator_category;
};
