#include "ImageBaking.hpp"
#include "core/IO.hpp"
#include "core/Result.hpp"

#include <cxxopts.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/std.h>

using namespace ren;

int main(int argc, const char *argv[]) {
  cxxopts::Options options("bake-specular-occlusion-lut",
                           "Bake specular occlusion LUT");
  // clang-format off
  options.add_options()
    ("out", "output path", cxxopts::value<fs::path>())
    ("no-compress", "don't compress")
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

  auto [blob_data, blob_size] =
      bake_so_lut_to_memory(baker, not result.count("no-compress")).value();
  stringify_and_write_to_files(blob_data, blob_size, path).value();

  ren::destroy_baker(baker);
}
