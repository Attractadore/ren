#pragma once
#include "app-base.hpp"
#include "ren/ren-dx12.hpp"

#include <SDL2/SDL_syswm.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <locale>

using Microsoft::WRL::ComPtr;

class DirectX12Renderer final : public Renderer {
  ComPtr<IDXGIFactory4> m_factory;
  ComPtr<IDXGIAdapter1> m_adapter;

public:
  Uint32 get_SDL2_flags() const override { return 0; }

  void create_instance() override {
    std::cout << "Create IDXGIFactory4\n";
    if (CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory))) {
      throw std::runtime_error{"DXGI : Failed to create factory"};
    }
  }

  bool select_adapter(unsigned idx) override {
    std::cout << "Create IDXGIAdapter1 #" << idx << "\n";
    return SUCCEEDED(
        m_factory->EnumAdapters1(idx, m_adapter.ReleaseAndGetAddressOf()));
  }

  std::string get_adapter_name() const override {
    assert(m_adapter);
    DXGI_ADAPTER_DESC1 desc;
    if (m_adapter->GetDesc1(&desc)) {
      throw std::runtime_error{"DXGI: Failed to get adapter description"};
    }
    struct codecvt_enabler : std::codecvt<wchar_t, char, std::mbstate_t> {};
    return std::wstring_convert<codecvt_enabler>().to_bytes(desc.Description);
  }

  ren::UniqueDevice create_device() override {
    std::cout << "Create ren::dx12::Device\n";
    DXGI_ADAPTER_DESC1 desc;
    if (m_adapter->GetDesc1(&desc)) {
      throw std::runtime_error{"DXGI: Failed to get adapter description"};
    }
    return ren::dx12::Device::create(desc.AdapterLuid);
  }

  ren::UniqueSwapchain create_swapchain(ren::Device &device,
                                        SDL_Window *window) override {
    std::cout << "Create ren::dx12::Swapchain\n";
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(window, &info);
    assert(info.subsystem == SDL_SYSWM_WINDOWS);
    auto *d3d_device = static_cast<ren::dx12::Device *>(&device);
    return d3d_device->create_swapchain(info.info.win.window);
  }
};
