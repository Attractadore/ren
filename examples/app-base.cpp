#include "app-base.hpp"

#if REN_VULKAN_RENDERER
#include "app-vk.hpp"
#endif

#if REN_DIRECTX12_RENDERER
#include "app-dx12.hpp"
#endif

#include <fmt/format.h>

void AppBase::select_renderer() {
  std::string ren_renderer = env("REN_RENDERER");
  if (ren_renderer.empty()) {
#if REN_DIRECTX12_RENDERER
    ren_renderer = "dx12";
#elif REN_VULKAN_RENDERER
    ren_renderer = "vk";
#else
#error No renderers enabled
#endif
  }

  if (false) {
  }
#if REN_VULKAN_RENDERER
  else if (ren_renderer == "vk") {
    m_renderer = std::make_unique<VulkanRenderer>();
  }
#endif
#if REN_DIRECTX12_RENDERER
  else if (ren_renderer == "dx12") {
    m_renderer = std::make_unique<DirectX12Renderer>();
  }
#endif
  else {
    throw std::runtime_error{
        fmt::format("ERROR: Invalid renderer \"{0}\"", ren_renderer)};
  }
}
