#pragma once
#include "Result.hpp"
#include "Span.hpp"
#include "StdDef.hpp"
#include "ren/ren.hpp"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace ren {

auto fopen(const fs::path &p, const char *mode) -> FILE *;

auto write_to_file(const void *data, usize size, const fs::path &p)
    -> Result<void, Error>;

auto stringify_and_write_to_files(const void *data, usize size,
                                  const fs::path &p) -> Result<void, Error>;

template <typename T>
auto write_to_file(Span<T> data, const fs::path &p) -> Result<void, Error> {
  return write_to_file(data.data(), data.size_bytes(), p);
}

template <typename T>
auto stringify_and_write_to_files(Span<T> data, const fs::path &p)
    -> Result<void, Error> {
  return stringify_and_write_to_files(data.data(), data.size_bytes(), p);
}

} // namespace ren
