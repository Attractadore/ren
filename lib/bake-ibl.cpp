#include "ImageBaking.hpp"
#include "core/Assert.hpp"
#include "core/IO.hpp"
#include "core/StdDef.hpp"
#include "ren/baking/baking.hpp"

#include <cxxopts.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/std.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <dlfcn.h>
#include <renderdoc_app.h>

using namespace ren;

RENDERDOC_API_1_0_0 *rdoc_api = nullptr;

int main(int argc, const char *argv[]) {
  if (void *mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD)) {
    pRENDERDOC_GetAPI RENDERDOC_GetAPI =
        (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
    int ret =
        RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_0_0, (void **)&rdoc_api);
    ren_assert(ret == 1);
  }

  cxxopts::Options options("bake-ibl", "Bake IBL for ren");
  // clang-format off
  options.add_options()
    ("in", "input HDR environment map path", cxxopts::value<fs::path>())
    ("out", "output filtered HDR environment cube map path", cxxopts::value<fs::path>())
    ("h,help", "show this message")
  ;
  // clang-format on
  options.parse_positional({"in", "out"});
  options.positional_help("in out");
  cxxopts::ParseResult result = options.parse(argc, argv);
  if (result.count("help") or not result.count("out")) {
    fmt::println("{}", options.help());
    return 0;
  }

  auto in_path = result["in"].as<fs::path>();
  auto out_path = result["out"].as<fs::path>();

  std::unique_ptr<IRenderer> renderer =
      create_renderer({.type = RendererType::Headless}).value();

  if (rdoc_api) {
    rdoc_api->StartFrameCapture(nullptr, nullptr);
  }

  IBaker *baker = create_baker(renderer.get()).value();

  FILE *f = fopen(in_path, "rb");
  if (!f) {
    fmt::println(stderr, "Failed to open {} for reading", in_path);
    return -1;
  }

  int w, h;
  const float *buffer = stbi_loadf_from_file(f, &w, &h, nullptr, 4);
  if (!buffer) {
    fmt::println(stderr, "Failed to read HDR environment map from {}: {}",
                 in_path, stbi_failure_reason());
    return -1;
  }
  std::fclose(f);

  Blob blob =
      bake_ibl_to_memory(baker,
                         {
                             .format = TinyImageFormat_R32G32B32A32_SFLOAT,
                             .width = (u32)w,
                             .height = (u32)h,
                             .data = buffer,
                         })
          .value();

  fs::path out_dir = out_path.parent_path();
  if (not out_dir.empty()) {
    fs::create_directory(out_dir);
  }

  write_to_file(blob.data, blob.size, out_path).value();

  destroy_baker(baker);

  if (rdoc_api) {
    rdoc_api->EndFrameCapture(nullptr, nullptr);
  }
}
