#include "BumpAllocator.hpp"
#include "RenderGraph.hpp"
#include "ResourceArena.hpp"
#include "ResourceUploader.hpp"
#include "ren/baking/image.hpp"

namespace DirectX {

struct Image;
struct ScratchImage;
struct TexMetadata;

} // namespace DirectX

namespace ren {

struct BakerPipelines {
  Handle<ComputePipeline> reflection_map;
  Handle<ComputePipeline> specular_map;
  Handle<ComputePipeline> irradiance_map;
};

struct Baker {
  Renderer *renderer = nullptr;
  ResourceArena session_arena;
  ResourceArena arena;
  Handle<CommandPool> cmd_pool;
  RgPersistent rg;
  DescriptorAllocator session_descriptor_allocator;
  DescriptorAllocatorScope descriptor_allocator;
  DeviceBumpAllocator allocator;
  UploadBumpAllocator upload_allocator;
  ResourceUploader uploader;
  BakerPipelines pipelines;
};

void reset_baker(Baker *baker);

auto to_dxtex_image(const TextureInfo &info) -> DirectX::Image;

auto to_dxtex_images(const TextureInfo &info, Vector<DirectX::Image> &images)
    -> DirectX::TexMetadata;

auto create_ktx_texture(const DirectX::ScratchImage &mip_chain)
    -> expected<ktxTexture2 *>;

auto write_ktx_to_memory(const DirectX::ScratchImage &mip_chain)
    -> expected<Blob>;

} // namespace ren
