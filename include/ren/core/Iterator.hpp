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

struct IteratorFacade {
  template <detail::CanDistanceTo Self>
  void equal(this const Self &self, const Self &other) {
    return self.distance_to(other) == 0;
  }

  void increment(this detail::CanAdvance auto &self) { self.advance(1); }

  void decrement(this detail::CanAdvance auto &self) { self.advance(-1); }

  decltype(auto) operator*(this const detail::CanRead auto &self) {
    return self.dereference();
  }

  template <typename Self>
  auto *operator->(this const Self &self)
    requires requires(Self &it) {
      {
        it.dereference()
      } -> std::convertible_to<const detail::DeduceIterValueTypeT<Self> &>;
    }
  {
    return &self.dereference();
  }

  decltype(auto) operator[](this auto &self, std::ptrdiff_t index) {
    return *(self + index);
  }

  template <detail::CanIncrement Self>
  auto operator++(this Self &self) -> Self & {
    self.increment();
    return self;
  }

  template <detail::CanIncrement Self>
  auto operator++(this Self &self, int) -> Self {
    Self old = self;
    self.increment();
    return old;
  }

  template <detail::CanDecrement Self>
  auto operator--(this Self &self) -> Self & {
    self.decrement();
    return self;
  }

  template <detail::CanDecrement Self>
  auto operator--(this Self &self, int) -> Self {
    Self tmp = self;
    self.decrement();
    return tmp;
  }

  template <detail::CanAdvance Self>
  auto operator+=(this Self &self, std::ptrdiff_t n) -> Self & {
    self.advance(n);
    return self;
  }

  template <detail::CanAdvance Self>
  auto operator-=(this Self &self, std::ptrdiff_t n) -> Self & {
    self.advance(-n);
    return self;
  }

  template <typename Self>
  auto operator+(this const Self &self, std::ptrdiff_t n) -> Self {
    Self tmp = self;
    return tmp += n;
  }

  template <typename Self>
  auto operator-(this const Self &self, std::ptrdiff_t n) -> Self {
    Self tmp = self;
    return tmp -= n;
  }

  template <detail::CanDistanceTo Self>
  auto operator-(this const Self &self, const Self &other) -> std::ptrdiff_t {
    return other.distance_to(self);
  }

  template <detail::CanCompare Self>
  bool operator==(this const Self &self, const Self &other) {
    return self.equal(other);
  }

  template <detail::CanDistanceTo Self>
  auto operator<=>(this const Self &self, const Self &other) {
    return self.distance_to(other) <=> 0;
  }
};

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
  requires std::derived_from<Iter, ren::IteratorFacade>
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
