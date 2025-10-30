#include "rhi.hpp"

#if __has_include(<renderdoc_app.h>)
#include <renderdoc_app.h>
#define RENDERDOC 1
#endif

#if __has_include(</usr/include/renderdoc_app.h>)
#include </usr/include/renderdoc_app.h>
#define RENDERDOC 1
#endif

#if __has_include(<C:/Program Files/RenderDoc/renderdoc_app.h>)
#include <C:/Program Files/RenderDoc/renderdoc_app.h>
#define RENDERDOC 1
#endif

#if RENDERDOC
#include <fmt/base.h>

#if _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ren::rhi {

static const RENDERDOC_API_1_0_0 *rdapi = nullptr;

void load_gfx_debugger() {
  if (rdapi) {
    return;
  }

  fmt::println("rhi: Load RenderDoc API");

  pRENDERDOC_GetAPI RENDERDOC_GetAPI = nullptr;
#if _WIN32
  HMODULE module = GetModuleHandleA("renderdoc.dll");
  if (!module) {
    fmt::println("rhi: Failed to load RenderDoc API");
    return;
  }
  RENDERDOC_GetAPI =
      (pRENDERDOC_GetAPI)GetProcAddress(module, "RENDERDOC_GetAPI");
#else
  void *lib_handle = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
  if (!lib_handle) {
    fmt::println("rhi: Failed to load RenderDoc API");
    return;
  }
  RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(lib_handle, "RENDERDOC_GetAPI");
#endif
  if (!RENDERDOC_GetAPI or
      !RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_0_0, (void **)&rdapi)) {
    fmt::println("rhi: Failed to load RenderDoc API");
    return;
  }
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

} // namespace ren::rhi

#else

namespace ren::rhi {

void load_gfx_debugger() {}

void start_gfx_capture() {}

void end_gfx_capture() {}

} // namespace ren::rhi

#endif
