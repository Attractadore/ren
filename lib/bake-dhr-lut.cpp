#include "ImageBaking.hpp"
#include "core/Result.hpp"
#include "core/Span.hpp"
#include "core/String.hpp"
#include "core/Views.hpp"

#include <cxxopts.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/std.h>
#include <fstream>

namespace fs = std::filesystem;
using namespace ren;

auto stringify(Span<const std::byte> data) -> String {
  constexpr usize LINE_WIDTH = 32;
  constexpr usize SYM_LENGTH = 6;
  String s;
  s.resize(data.size() * SYM_LENGTH);
  constexpr char REMAP[] = {
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
  };
  usize b = 0;
  usize c = 0;
  while (b < data.size()) {
    usize len = std::min(data.size() - b, LINE_WIDTH);
    for (usize k : range(len)) {
      s[c++] = '0';
      s[c++] = 'x';
      s[c++] = REMAP[((u8)data[b] >> 4)];
      s[c++] = REMAP[(u8)data[b] & 0xf];
      s[c++] = ',';
      s[c++] = ' ';
      b++;
    }
    s[c - 1] = '\n';
  }
  ren_assert(c == s.size());
  return s;
}

auto write_to_file(Span<const std::byte> data, const fs::path &path)
    -> Result<void, Error> {
  fs::path header_path = fmt::format("{}.inc", path);
  auto header = stringify(data);

  fs::path parent_dir = path.parent_path();
  if (not fs::exists(parent_dir)) {
    fs::create_directory(parent_dir);
  }

  std::ofstream f(path, std::ios::binary);
  if (!f) {
    fmt::println(stderr, "Failed to open {} for writing", path);
    return Failure(Error::IO);
  }
  f.write((const char *)data.data(), data.size());
  if (!f) {
    fmt::println(stderr, "Failed to open write {}", path);
    return Failure(Error::IO);
  }
  f.close();

  f.open(header_path, std::ios::binary);
  if (!f) {
    fmt::println(stderr, "Failed to open {} for writing", header_path);
    return Failure(Error::IO);
  }
  f.write(header.data(), header.size());
  if (!f) {
    fmt::println(stderr, "Failed to open write {}", header_path);
    return Failure(Error::IO);
  }

  return {};
}

int main(int argc, const char *argv[]) {
  cxxopts::Options options("bake-dhr-lut", "Bake DHR LUT");
  // clang-format off
  options.add_options()
    ("out", "output path", cxxopts::value<fs::path>())
    ("h,help", "show this message")
  ;
  // clang-format on
  options.parse_positional({"out"});
  options.positional_help("out");
  cxxopts::ParseResult result = options.parse(argc, argv);
  if (result.count("help") or not result.count("out")) {
    fmt::println("{}", options.help());
    return 0;
  }

  auto path = result["out"].as<fs::path>();

  std::unique_ptr<IRenderer> renderer =
      ren::create_renderer({.type = RendererType::Headless}).value();
  IBaker *baker = ren::create_baker(renderer.get()).value();

  auto [blob_data, blob_size] = bake_dhr_lut_to_memory(baker).value();
  write_to_file(Span((const std::byte *)blob_data, blob_size), path).value();

  ren::destroy_baker(baker);
}
