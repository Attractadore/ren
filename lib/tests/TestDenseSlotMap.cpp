#include "Support/DenseSlotMap.hpp"
#include "Support/Views.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>

namespace ren {

std::ostream &operator<<(std::ostream &os, SlotMapKey key) {
  struct Hack {
    uint32_t index : 24;
    uint32_t generation : 8;
  };
  auto hack = std::bit_cast<Hack>(key);
  os << "idx: " << hack.index << ", gen: " << hack.generation;
  return os;
}
} // namespace ren

using namespace ren;

template class ren::DenseSlotMap<int>;

using TestIterator = DenseSlotMap<int>::iterator;
static_assert(std::input_iterator<TestIterator>);
static_assert(std::forward_iterator<TestIterator>);
static_assert(std::bidirectional_iterator<TestIterator>);
static_assert(std::random_access_iterator<TestIterator>);

TEST(TestIsEmpty, Empty) {
  DenseSlotMap<int> s;
  EXPECT_TRUE(s.empty());
}

TEST(TestIsEmpty, NotEmpty) {
  DenseSlotMap<int> s;
  auto k = s.insert(1);
  EXPECT_FALSE(s.empty());
}

TEST(TestSize, InsertAndDelete) {
  DenseSlotMap<int> s;
  std::vector<decltype(s)::key_type> keys;
  int i = 0;
  for (; i < 16; i++) {
    EXPECT_EQ(s.size(), i);
    auto k = s.insert(i);
    keys.push_back(k);
  }
  for (; i > 0; i--) {
    EXPECT_EQ(s.size(), i);
    auto k = keys.back();
    keys.pop_back();
    auto it = s.find(k);
    ASSERT_NE(it, s.end());
    s.erase(it);
  }
  EXPECT_EQ(s.size(), i);
}

TEST(TestCapacity, Fresh) {
  DenseSlotMap<int> s;
  ASSERT_EQ(s.capacity(), 0);
}

TEST(TestCapacity, AfterReserve) {
  DenseSlotMap<int> s;
  s.reserve(10);
  ASSERT_GE(s.capacity(), 10);
}

TEST(TestCapacity, AfterShrink) {
  DenseSlotMap<int> s;
  s.reserve(10);
  auto cnt = s.size();
  s.shrink_to_fit();
  ASSERT_LE(s.capacity(), cnt);
}

TEST(TestClear, Clear) {
  DenseSlotMap<int> s;
  for (size_t i = 0; i < 16; i++) {
    auto k = s.insert(i);
  }
  s.clear();
  EXPECT_TRUE(s.empty());
}

TEST(TestClear, ValueAfterInsertAfterClear) {
  DenseSlotMap<int> s;
  for (size_t i = 0; i < 3; i++) {
    auto k = s.insert(i);
  }
  s.clear();
  EXPECT_TRUE(s.empty());
  std::vector values = {0, 1, 2, 3, 4};
  for (auto v : values) {
    auto k = s.insert(v);
    auto it = s.find(k);
    ASSERT_NE(it, s.end()) << "Iterator for value " << v << " and key (" << k
                           << ") is end()";
    auto [fk, fv] = *it;
    EXPECT_EQ(fk, k);
    EXPECT_EQ(fv, v);
  }
}

TEST(TestInsert, ValueAfterInsert) {
  DenseSlotMap<int> s;
  std::vector values = {0, 1, 2, 3, 4};
  for (auto v : values) {
    auto k = s.insert(v);
    auto it = s.find(k);
    ASSERT_NE(it, s.end()) << "Iterator for value " << v << " and key (" << k
                           << ") is end()";
    auto [fk, fv] = *it;
    EXPECT_EQ(fk, k);
    EXPECT_EQ(fv, v);
  }
}

TEST(TestInsert, ValueAfterAllInsert) {
  DenseSlotMap<int> s;
  std::vector values = {0, 1, 2, 3, 4};
  std::vector<decltype(s)::key_type> keys;
  for (auto v : values) {
    keys.push_back(s.insert(v));
  }
  for (size_t i = 0; i < values.size(); i++) {
    auto k = keys[i];
    auto v = values[i];
    auto it = s.find(k);
    ASSERT_NE(it, s.end()) << "Iterator for value " << v << " and key (" << k
                           << ") is end()";
    auto [fk, fv] = *it;
    EXPECT_EQ(fk, k);
    EXPECT_EQ(fv, v);
  }
}

TEST(TestInsert, InsertAfterErase) {
  DenseSlotMap<int> s;

  auto base_k = s.insert(0);
  auto old_k = s.insert(1);
  s.erase(old_k);
  auto new_k = s.insert(2);
  ASSERT_NE(new_k, base_k);
  ASSERT_NE(new_k, old_k);

  auto base_it = s.find(base_k);
  auto old_it = s.find(old_k);
  auto new_it = s.find(new_k);

  ASSERT_NE(base_it, s.end());
  ASSERT_EQ(old_it, s.end());
  ASSERT_NE(new_it, s.end());

  EXPECT_EQ(std::get<1>(*base_it), 0);
  EXPECT_EQ(std::get<1>(*new_it), 2);
}

TEST(TestEmplace, Emplace) {
  DenseSlotMap<int> s;
  std::vector values = {0, 1, 2, 3, 4};
  for (auto v : values) {
    auto k = s.emplace(v);
    EXPECT_EQ(s[k], v);
  }
}

TEST(TestEmplace, EmplaceAfterErase) {
  DenseSlotMap<int> s;

  auto base_k = s.emplace(0);
  auto old_k = s.emplace(1);
  s.erase(old_k);
  auto new_k = s.emplace(2);

  auto base_it = s.find(base_k);
  auto old_it = s.find(old_k);
  auto new_it = s.find(new_k);

  ASSERT_NE(base_it, s.end());
  ASSERT_EQ(old_it, s.end());
  ASSERT_NE(new_it, s.end());

  EXPECT_EQ(std::get<1>(*base_it), 0);
  EXPECT_EQ(std::get<1>(*new_it), 2);
}

TEST(TestErase, EraseIterator) {
  DenseSlotMap<int> s;

  auto k = s.insert(0);
  auto it = s.find(k);
  ASSERT_NE(it, s.end());

  s.erase(it);
  EXPECT_EQ(s.size(), 0);
  EXPECT_EQ(s.find(k), s.end());
}

TEST(TestErase, EraseAllIterator) {
  DenseSlotMap<int> s;
  std::vector values = {0, 1, 2, 3, 4};
  for (auto v : values) {
    auto k = s.insert(v);
  }
  for (auto it = s.begin(); it != s.end();) {
    it = s.erase(it);
  }
  EXPECT_TRUE(s.empty());
}

TEST(TestErase, EraseAllIteratorBackward) {
  DenseSlotMap<int> s;
  std::vector values = {0, 1, 2, 3, 4};
  for (auto v : values) {
    auto k = s.insert(v);
  }
  while (not s.empty()) {
    s.erase(s.end() - 1);
  }
  EXPECT_TRUE(s.empty());
}

TEST(TestErase, EraseKey) {
  DenseSlotMap<int> s;

  auto k = s.insert(0);

  s.erase(k);
  EXPECT_EQ(s.size(), 0);
  EXPECT_EQ(s.find(k), s.end());
}

TEST(TestTryErase, TryErase) {
  DenseSlotMap<int> s;

  auto k = s.insert(0);

  EXPECT_TRUE(s.try_erase(k));
  EXPECT_FALSE(s.try_erase(k));
}

TEST(TestSwap, Swap) {
  DenseSlotMap<int> s1, s2;
  using key_type = decltype(s1)::key_type;
  std::vector<key_type> keys1;
  std::vector<key_type> keys2;
  std::array vals = {0, 1, 2, 3, 4, 5};

  int i = 0;
  for (; i < 4; i++) {
    keys1.push_back(s1.insert(vals[i]));
  }
  for (; i < vals.size(); i++) {
    keys2.push_back(s2.insert(vals[i]));
  }

  swap(s1, s2);

  i = 0;
  for (; i < 4; i++) {
    auto k = keys1[i];
    EXPECT_EQ(s2[k], vals[i]);
  }
  for (; i < vals.size(); i++) {
    auto k = keys2[i - 4];
    EXPECT_EQ(s1[k], vals[i]);
  }
}

TEST(TestFind, FindPresent) {
  DenseSlotMap<int> s;
  auto val = 0xffdead;
  auto k = s.insert(val);
  auto it = s.find(k);
  ASSERT_NE(it, s.end());
  EXPECT_EQ(std::get<1>(*it), val);
}

TEST(TestFind, FindNotPresent) {
  DenseSlotMap<int> s;
  auto val = 0xffdead;
  auto k = s.insert(val);
  s.erase(k);
  auto it = s.find(k);
  EXPECT_EQ(it, s.end());
}

TEST(TestFind, FindAfterClear) {
  DenseSlotMap<int> s;
  auto val = 0xffdead;
  auto k = s.insert(val);
  s.clear();
  auto it = s.find(k);
  EXPECT_EQ(it, s.end());
}

TEST(TestGet, GetPresent) {
  DenseSlotMap<int> s;
  auto val = 0xffdead;
  auto k = s.insert(val);
  auto opt = s.get(k);
  EXPECT_EQ(opt, val);
}

TEST(TestGet, GetNotPresent) {
  DenseSlotMap<int> s;
  auto val = 0xffdead;
  auto k = s.insert(val);
  s.erase(k);
  auto opt = s.get(k);
  EXPECT_EQ(opt, None);
}

TEST(TestAccess, Access) {
  DenseSlotMap<int> s;
  auto val = 0xffdead;
  auto k = s.insert(val);
  EXPECT_EQ(s[k], val);
}

TEST(TestContains, ContainsPresent) {
  DenseSlotMap<int> s;
  auto val = 0xffdead;
  auto k = s.insert(val);
  EXPECT_TRUE(s.contains(k));
}

TEST(TestContains, ContainsNotPresent) {
  DenseSlotMap<int> s;
  auto val = 0xffdead;
  auto k = s.insert(val);
  s.erase(k);
  EXPECT_FALSE(s.contains(k));
}

TEST(TestContains, ContainsAfterClear) {
  DenseSlotMap<int> s;
  auto val = 0xffdead;
  auto k = s.insert(val);
  s.clear();
  EXPECT_FALSE(s.contains(k));
}

TEST(TestCompare, Self) {
  DenseSlotMap<int> s1;
  EXPECT_EQ(s1, s1);
}

TEST(TestCompare, Copy) {
  DenseSlotMap<int> s1;
  auto s2 = s1;
  EXPECT_EQ(s1, s2);
}

TEST(TestCompare, DifferentEmpty) {
  DenseSlotMap<int> s1;
  DenseSlotMap<int> s2;
  EXPECT_EQ(s1, s2);
}

TEST(TestCompare, DifferentNotEmpty) {
  DenseSlotMap<int> s1;
  auto k = s1.insert(0);
  DenseSlotMap<int> s2;
  EXPECT_NE(s1, s2);
}

TEST(TestCompare, DifferentSame) {
  DenseSlotMap<int> s1;
  for (int i = 0; i < 16; i++) {
    auto k = s1.insert(i);
  }
  DenseSlotMap<int> s2;
  for (int i = 0; i < 16; i++) {
    EXPECT_NE(s1, s2);
    auto k = s2.insert(i);
  }
  EXPECT_EQ(s1, s2);
}

TEST(TestPop, Pop) {
  DenseSlotMap<int> s;

  auto k = s.insert(0);

  auto v = s.pop(k);
  EXPECT_EQ(s.size(), 0);
  EXPECT_EQ(s.find(k), s.end());
  EXPECT_EQ(v, 0);
}

TEST(TestPop, PopAll) {
  DenseSlotMap<int> s;
  using Key = decltype(s)::key_type;

  std::vector values = {0, 1, 2, 3, 4};
  std::vector<Key> keys;

  for (auto v : values) {
    auto k = s.insert(v);
    keys.push_back(k);
  }
  for (size_t i = 0; i < values.size(); i++) {
    auto k = keys[i];
    EXPECT_EQ(s.size(), values.size() - i);
    auto v = s.pop(k);
    EXPECT_EQ(v, values[i]);
  }
  EXPECT_TRUE(s.empty());
}

TEST(TestTryPop, TryPop) {
  DenseSlotMap<int> s;

  auto val = 0;
  auto key = s.insert(val);

  EXPECT_EQ(s.try_pop(key), val);
  EXPECT_EQ(s.try_pop(key), None);
}

TEST(TestKeysValues, KeysValues) {
  DenseSlotMap<int> s;
  std::vector values = {0, 1, 2, 3, 4};
  std::vector<decltype(s)::key_type> keys;
  for (auto v : values) {
    keys.push_back(s.insert(v));
  }
  EXPECT_TRUE(std::ranges::is_permutation(s.keys(), keys));
  EXPECT_TRUE(std::ranges::is_permutation(s.values(), values));
}

TEST(TestEraseIf, EraseIf) {
  DenseSlotMap<int> s;
  std::vector values = {0, 1, 2, 3, 4};
  std::vector<SlotMapKey> keys;
  for (auto value : values) {
    keys.push_back(s.insert(value));
  }
  auto pred = [](SlotMapKey _, int value) { return value % 2; };
  s.erase_if(pred);
  for (auto [k, v] : zip(keys, values)) {
    if (pred(k, v)) {
      EXPECT_FALSE(s.contains(k));
    } else {
      EXPECT_TRUE(s.contains(k));
      EXPECT_EQ(s[k], v);
    }
  }
}
