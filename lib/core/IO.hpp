#pragma once
#include "Result.hpp"
#include "Span.hpp"
#include "StdDef.hpp"
#include "String.hpp"
#include "ren/ren.hpp"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace ren {

// Returns Linux path if running on Wine, otherwise the path.
auto to_system_path(const fs::path &p) -> String;

auto fopen(const fs::path &p, const char *mode) -> FILE *;

auto write_to_file(void *data, usize size, fs::path &p) -> Result<void, Error>;

template <typename T>
auto write_to_file(Span<T> data, fs::path &p) -> Result<void, Error> {
  return write_to_file(data.data(), data.size_bytes(), p);
}

} // namespace ren
