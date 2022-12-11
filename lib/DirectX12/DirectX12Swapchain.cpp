#include "DirectX12/DirectX12Swapchain.hpp"
#include "BlitTexture2D.h"
#include "DirectX12/DXGIFormat.hpp"
#include "DirectX12/DirectX12CommandAllocator.hpp"
#include "DirectX12/DirectX12Device.hpp"
#include "DirectX12/DirectX12Texture.hpp"
#include "DirectX12/Errors.hpp"
#include "hlsl/Texture2DBlitConfig.hlsl"

namespace ren {
namespace {
ID3D12RootSignature *createBlitRootSignature(ID3D12Device *device) {
  D3D12_STATIC_SAMPLER_DESC sampler_desc = {
      .Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
      .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      .ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
      .MaxLOD = D3D12_FLOAT32_MAX,
  };

  std::array<D3D12_DESCRIPTOR_RANGE1, 2> table_ranges;
  table_ranges[0] = {
      .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
      .NumDescriptors = 1,
      .Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
  };
  table_ranges[1] = {
      .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
      .NumDescriptors = 1,
      .Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
      .OffsetInDescriptorsFromTableStart = 1,
  };

  std::array<D3D12_ROOT_PARAMETER1, 2> params;
  params[0] = {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
               .DescriptorTable = {.NumDescriptorRanges = table_ranges.size(),
                                   .pDescriptorRanges = table_ranges.data()}};
  params[1] = {
      .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
      .Constants = {.Num32BitValues =
                        sizeof(Texture2DBlitConfig) / sizeof(uint32_t)},
  };

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc = {
      .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
      .Desc_1_1 = {
          .NumParameters = params.size(),
          .pParameters = params.data(),
          .NumStaticSamplers = 1,
          .pStaticSamplers = &sampler_desc,
      }};

  ComPtr<ID3DBlob> root_sig_blob;
  throwIfFailed(D3D12SerializeVersionedRootSignature(&root_sig_desc,
                                                     &root_sig_blob, nullptr),
                "D3D12: Failed to serialize root signature");

  ID3D12RootSignature *root_sig;
  throwIfFailed(device->CreateRootSignature(
                    0, root_sig_blob->GetBufferPointer(),
                    root_sig_blob->GetBufferSize(), IID_PPV_ARGS(&root_sig)),
                "D3D12: Failed to create root signature");

  return root_sig;
}

ID3D12PipelineState *createBlitPSO(ID3D12Device *device,
                                   ID3D12RootSignature *root_sig) {
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {
      .pRootSignature = root_sig,
      .CS = {.pShaderBytecode = BlitTexture2DShader,
             .BytecodeLength = sizeof(BlitTexture2DShader)},
  };
  ID3D12PipelineState *pso;
  throwIfFailed(
      device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso)),
      "D3D12: Failed to create pipeline state");
  return pso;
}
} // namespace

DirectX12Swapchain::DirectX12Swapchain(DirectX12Device *device, HWND hwnd) {
  m_device = device;
  m_hwnd = hwnd;
  DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {.Count = 1},
      .BufferCount = c_buffer_count,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
  };
  ComPtr<IDXGISwapChain1> swapchain;
  throwIfFailed(m_device->getDXGIFactory()->CreateSwapChainForHwnd(
                    m_device->getDirectQueue(), m_hwnd, &swapchain_desc,
                    nullptr, nullptr, &swapchain),
                "DXGI: Failed to create swapchain");
  throwIfFailed(swapchain.As(&m_swapchain), "DXGI: Failed to create swapchain");
  m_textures.resize(c_buffer_count);
  setTextures();

  m_blit_root_sig = createBlitRootSignature(m_device->get());
  m_blit_pso = createBlitPSO(m_device->get(), m_blit_root_sig.Get());
}

void DirectX12Swapchain::setTextures() {
  for (int i = 0; i < c_buffer_count; ++i) {
    ComPtr<ID3D12Resource> surface;
    m_swapchain->GetBuffer(i, IID_PPV_ARGS(&surface));
    auto desc = surface->GetDesc();
    m_textures[i] = {
        .desc =
            {
                .type = TextureType::e2D,
                .format = getFormat(desc.Format),
                .usage = getTextureUsageFlags(desc.Flags),
                .width = static_cast<unsigned>(desc.Width),
                .height = desc.Height,
                .layers = desc.DepthOrArraySize,
                .levels = desc.MipLevels,
            },
        .handle = AnyRef(surface.Get(),
                         [device = m_device](ID3D12Resource *resource) {
                           device->destroyResourceData(resource);
                         }),
    };
  }
}

namespace {
std::tuple<unsigned, unsigned> getWindowSize(HWND hwnd) {
  RECT rect;
  throwIfFailed(!!GetClientRect(hwnd, &rect),
                "WIN32: Failed to get window size");
  return {rect.right, rect.bottom};
}

std::tuple<unsigned, unsigned> getSwapchainSize(IDXGISwapChain1 *swapchain) {
  DXGI_SWAP_CHAIN_DESC1 desc;
  throwIfFailed(swapchain->GetDesc1(&desc),
                "DXGI: Failed to get swapchain description");
  return {desc.Width, desc.Height};
}
} // namespace

void DirectX12Swapchain::AcquireBuffer(DirectX12CommandAllocator &cmd_alloc) {
  // If the swapchain's window is minimized, don't do anything
  if (IsIconic(m_hwnd)) {
    return;
  }
  auto window_size = getWindowSize(m_hwnd);
  auto swapchain_size = getSwapchainSize(m_swapchain.Get());
  if (window_size != swapchain_size) {
    // All accesses to the swapchain's buffers must be completed and all
    // references to them must be released.
    cmd_alloc.flush();
    throwIfFailed(m_swapchain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0),
                  "DXGI: Failed to resize swapchain");
    setTextures();
    // NOTE: swapchain size might still not match window size
  }
}

void DirectX12Swapchain::PresentBuffer() {
  throwIfFailed(m_swapchain->Present(1, 0),
                "DXGI: Failed to present swapchain buffer");
}
} // namespace ren
