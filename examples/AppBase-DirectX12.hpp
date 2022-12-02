#include "ren/ren-dx12.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cassert>
#include <iostream>
#include <locale>
#include <vector>

using Microsoft::WRL::ComPtr;

inline ComPtr<IDXGIFactory4> createDXGIFactory() {
  ComPtr<IDXGIFactory4> factory;
  if (CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) {
    throw std::runtime_error{"DXGI : Failed to create factory"};
  }
  return factory;
}

inline ComPtr<IDXGIAdapter1> selectAdapter(IDXGIFactory4 *factory,
                                           unsigned idx = 0) {
  std::vector<ComPtr<IDXGIAdapter1>> adapters;
  while (true) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(adapters.size(), &adapter) ==
        DXGI_ERROR_NOT_FOUND) {
      break;
    }
    adapters.push_back(std::move(adapter));
  }
  if (adapters.empty()) {
    throw std::runtime_error{"DXGI: Failed to find an adapter"};
  }
  if (idx >= adapters.size()) {
    throw std::runtime_error{"DXGI: Adapter index out of range"};
  }
  return std::move(adapters[idx]);
}

inline std::string getAdapterName(IDXGIAdapter1 *adapter) {
  DXGI_ADAPTER_DESC1 desc;
  if (adapter->GetDesc1(&desc)) {
    throw std::runtime_error{"DXGI: Failed to get adapter description"};
  }
  struct codecvt_enabler : std::codecvt<wchar_t, char, std::mbstate_t> {};
  return std::wstring_convert<codecvt_enabler>().to_bytes(desc.Description);
}

inline LUID getAdapterLUID(IDXGIAdapter1 *adapter) {
  DXGI_ADAPTER_DESC1 desc;
  if (adapter->GetDesc1(&desc)) {
    throw std::runtime_error{"DXGI: Failed to get adapter description"};
  }
  return desc.AdapterLuid;
}

inline HWND getWindowHWND(SDL_Window *window) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  SDL_GetWindowWMInfo(window, &info);
  assert(info.subsystem == SDL_SYSWM_WINDOWS);
  return info.info.win.window;
}

template <class Derived> class AppBase {
  using WindowDeleter = decltype([](SDL_Window *window) {
    SDL_DestroyWindow(window);
  });

  std::string m_app_name;
  struct SDL {
    SDL() { SDL_Init(SDL_INIT_EVERYTHING); }
    ~SDL() { SDL_Quit(); }
  } SDL;
  std::unique_ptr<SDL_Window, WindowDeleter> m_window;
  ren::dx12::UniqueDevice m_device;
  ren::dx12::UniqueSwapchain m_swapchain;
  ren::UniqueScene m_scene;

public:
  AppBase(std::string app_name);
  void run();

protected:
  void processEvent(const SDL_Event &e) {}
  void iterate() {}

private:
  void mainLoop();
};

template <class Derived> inline void AppBase<Derived>::run() {
  auto *impl = static_cast<Derived *>(this);
  bool quit = false;
  while (!quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        quit = true;
      }
      impl->processEvent(e);
    }

    int w, h;
    SDL_GetWindowSize(m_window.get(), &w, &h);
    m_scene->setOutputSize(w, h);
    m_swapchain->setSize(w, h);

    impl->iterate();
    m_scene->draw();
  }
  std::cout << "Done\n";
}

template <class Derived>
inline AppBase<Derived>::AppBase(std::string app_name)
    : m_app_name(std::move(app_name)) {
  std::cout << "Create SDL_Window\n";
  m_window.reset(SDL_CreateWindow(m_app_name.c_str(), SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED, 1280, 720,
                                  SDL_WINDOW_RESIZABLE));

  std::cout << "Create IDXGIFactory4\n";
  auto dxgi_factory = createDXGIFactory();

  std::cout << "Select IDXGIAdapter1\n";
  auto adapter = selectAdapter(dxgi_factory.Get());
  std::cout << "Running on " << getAdapterName(adapter.Get()) << "\n";
  std::cout << "Create ren::Device\n";
  m_device = ren::dx12::Device::create(getAdapterLUID(adapter.Get()));

  std::cout << "Create ren::Swapchain\n";
  m_swapchain = m_device->createSwapchain(getWindowHWND(m_window.get()));

  std::cout << "Create ren::Scene\n";
  m_scene = m_device->createScene();
  m_scene->setSwapchain(m_swapchain.get());
}
