#include "rhi.hpp"

#if __has_include(<renderdoc_app.h>)
#define RENDERDOC 1
#endif

#if RENDERDOC
#include <fmt/format.h>
#include <renderdoc_app.h>

#if __linux__
#include <dlfcn.h>
#endif

namespace ren::rhi {

static const RENDERDOC_API_1_6_0 *rdapi = nullptr;

auto load_gfx_debugger() -> Result<void> {
  if (rdapi) {
    return {};
  }

  fmt::println("rhi: Load RenderDoc API");

#if __linux__
  void *mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
  if (!mod) {
    fmt::println("rhi: Failed to load RenderDoc API");
    return fail(Error::FeatureNotPresent);
  }
  pRENDERDOC_GetAPI RENDERDOC_GetAPI =
      (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
#else
  todo();
#endif
  if (!RENDERDOC_GetAPI or
      !RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_0_0, (void **)&rdapi)) {
    fmt::println("rhi: Failed to load RenderDoc API");
    return fail(Error::FeatureNotPresent);
  }

  return {};
}

void start_gfx_capture() {
  if (rdapi) {
    rdapi->StartFrameCapture(nullptr, nullptr);
  }
}

void end_gfx_capture() {
  if (rdapi) {
    rdapi->EndFrameCapture(nullptr, nullptr);
  }
}

auto have_gfx_debugger() -> bool { return rdapi != nullptr; }

void set_gfx_capture_title(StringView name) {
  if (rdapi) {
    char c_str[256];
    name = name.substr(0, std::min(std::size(c_str) - 1, name.size()));
    std::ranges::copy(name, c_str);
    c_str[name.size()] = '\0';
    rdapi->SetCaptureTitle(c_str);
  }
}

} // namespace ren::rhi

#else

namespace ren::rhi {

auto load_gfx_debugger() -> Result<void> {
  return fail(Error::FeatureNotPresent);
}

void start_gfx_capture() {}

void end_gfx_capture() {}

auto have_gfx_debugger() -> bool { return false; }

void set_gfx_capture_title(StringView) {}

} // namespace ren::rhi

#endif

namespace ren::rhi {

void start_gfx_capture(StringView name) {
  start_gfx_capture();
  set_gfx_capture_title(name);
}

} // namespace ren::rhi
