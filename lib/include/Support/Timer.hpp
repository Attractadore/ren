#pragma once
#include "Macros.hpp"
#include "String.hpp"

#include <fmt/core.h>

#include <chrono>

namespace ren {

class RegionTimer {
public:
  explicit RegionTimer(String name) {
    m_name = std::move(name);
    m_start = std::chrono::steady_clock::now();
  }

  RegionTimer(const RegionTimer &) = delete;
  RegionTimer(RegionTimer &&) = delete;

  ~RegionTimer() {
    auto end = std::chrono::steady_clock::now();
    fmt::println(
        stderr, "{}: {} us", m_name,
        std::chrono::duration_cast<std::chrono::microseconds>(end - m_start)
            .count());
  }

  RegionTimer &operator=(const RegionTimer &) = delete;
  RegionTimer &operator=(RegionTimer &&) = delete;

private:
  String m_name;
  std::chrono::steady_clock::time_point m_start;
};

#define ren_region_timer_name ren_cat(region_timer_, __LINE__)
#define ren_time_region(name) RegionTimer ren_region_timer_name(name)

class TimeCounter {
public:
  friend class TimeCounterGuard;

  explicit TimeCounter(String name) { m_name = std::move(name); }

  void dump() {
    fmt::println(
        stderr, "{}: {} us", m_name,
        std::chrono::duration_cast<std::chrono::microseconds>(m_duration)
            .count());
  }

private:
  String m_name;
  std::chrono::nanoseconds m_duration{0};
};

class TimeCounterGuard {
public:
  explicit TimeCounterGuard(TimeCounter &counter) {
    m_counter = &counter;
    m_start = std::chrono::steady_clock::now();
  }

  TimeCounterGuard(const TimeCounterGuard &) = delete;
  TimeCounterGuard(TimeCounterGuard &&) = delete;

  ~TimeCounterGuard() {
    auto end = std::chrono::steady_clock::now();
    m_counter->m_duration += end - m_start;
  }

  TimeCounterGuard &operator=(const TimeCounterGuard &) = delete;
  TimeCounterGuard &operator=(TimeCounterGuard &&) = delete;

private:
  std::chrono::steady_clock::time_point m_start;
  TimeCounter *m_counter = nullptr;
};

#define ren_time_counter_guard_name ren_cat(time_counter_guard, __LINE__)
#define ren_inc_time_counter(counter)                                          \
  TimeCounterGuard ren_time_counter_guard_name(counter)

} // namespace ren
