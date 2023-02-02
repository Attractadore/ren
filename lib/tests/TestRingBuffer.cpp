#include "RingBuffer.hpp"

#include <gtest/gtest.h>

using namespace ren;

TEST(RingBufferTest, Write) {
  RingBufferAllocator rb(10);

  auto [offset, size] = rb.write(9, 1, 1);
  EXPECT_EQ(offset, 0);
  EXPECT_EQ(size, 9);
}

TEST(RingBufferTest, WriteSomeSpace) {
  RingBufferAllocator rb(10);

  {
    auto [offset, size] = rb.write(9, 1, 1);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(size, 9);
  }
  rb.next_frame();

  {
    auto [offset, size] = rb.write(1, 1, 1);
    EXPECT_EQ(offset, 9);
    EXPECT_EQ(size, 1);
  }
}

TEST(RingBufferTest, WriteNoSpace) {
  RingBufferAllocator rb(10);

  {
    auto [offset, size] = rb.write(9, 1, 1);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(size, 9);
  }
  rb.next_frame();

  {
    auto [offset, count] = rb.write(1, 4, 1);
    EXPECT_EQ(count, 0);
  }
}

TEST(RingBufferTest, WriteSpaceFront) {
  RingBufferAllocator rb(10);

  {
    auto [offset, size] = rb.write(9, 1, 1);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(size, 9);
  }
  rb.next_frame();

  rb.next_frame();

  {
    auto [offset, count] = rb.write(1, 4, 1);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(count, 1);
  }
}

TEST(RingBufferTest, WriteAligned) {
  RingBufferAllocator rb(12);

  {
    auto [offset, size] = rb.write(1, 1, 1);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(size, 1);
  }
  {
    auto [offset, count] = rb.write(1, 1, 4);
    EXPECT_EQ(offset, 4);
    EXPECT_EQ(count, 1);
  }
}

TEST(RingBufferTest, WriteAlignedEnd) {
  RingBufferAllocator rb(12);

  {
    auto [offset, size] = rb.write(9, 1, 1);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(size, 9);
  }
  rb.next_frame();

  rb.next_frame();

  {
    auto [offset, count] = rb.write(1, 1, 4);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(count, 1);
  }
}
