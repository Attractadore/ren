#include "RingBuffer.hpp"

#include <gtest/gtest.h>

using namespace ren;

TEST(RingBufferTest, Write) {
  RingBufferAllocator rb(10);

  rb.begin_frame();
  auto [offset, size] = rb.write(9, 1, 1);
  EXPECT_EQ(offset, 0);
  EXPECT_EQ(size, 9);
  rb.end_frame();
}

TEST(RingBufferTest, WriteSomeSpace) {
  RingBufferAllocator rb(10);

  rb.begin_frame();
  {
    auto [offset, size] = rb.write(9, 1, 1);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(size, 9);
  }
  rb.end_frame();

  rb.begin_frame();
  {
    auto [offset, size] = rb.write(1, 1, 1);
    EXPECT_EQ(offset, 9);
    EXPECT_EQ(size, 1);
  }
  rb.end_frame();
}

TEST(RingBufferTest, WriteNoSpace) {
  RingBufferAllocator rb(10);

  rb.begin_frame();
  {
    auto [offset, size] = rb.write(9, 1, 1);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(size, 9);
  }
  rb.end_frame();

  rb.begin_frame();
  {
    auto [offset, count] = rb.write(1, 4, 1);
    EXPECT_EQ(count, 0);
  }
  rb.end_frame();
}

TEST(RingBufferTest, WriteSpaceFront) {
  RingBufferAllocator rb(10);

  rb.begin_frame();
  {
    auto [offset, size] = rb.write(9, 1, 1);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(size, 9);
  }
  rb.end_frame();

  rb.begin_frame();
  rb.end_frame();

  rb.begin_frame();
  {
    auto [offset, count] = rb.write(1, 4, 1);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(count, 1);
  }
  rb.end_frame();
}

TEST(RingBufferTest, WriteAligned) {
  RingBufferAllocator rb(12);

  rb.begin_frame();
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
  rb.end_frame();
}

TEST(RingBufferTest, WriteAlignedEnd) {
  RingBufferAllocator rb(12);

  rb.begin_frame();
  {
    auto [offset, size] = rb.write(9, 1, 1);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(size, 9);
  }
  rb.end_frame();

  rb.begin_frame();
  rb.end_frame();

  rb.begin_frame();
  {
    auto [offset, count] = rb.write(1, 1, 4);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(count, 1);
  }
  rb.end_frame();
}
