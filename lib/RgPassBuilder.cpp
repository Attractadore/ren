#include "RenderGraph.hpp"

namespace ren {

RgPassBuilder::PassBuilder(RgPass pass, Builder &builder)
    : m_pass(pass), m_builder(&builder) {}

auto RgPassBuilder::create_buffer(RgBufferCreateInfo &&create_info,
                                  const RgBufferAccessDesc &access)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  return m_builder->create_buffer(m_pass, std::move(create_info), access);
}

auto RgPassBuilder::read_buffer(RgBufferReadInfo &&read_info,
                                const RgBufferAccessDesc &access)
    -> RgRtBuffer {
  return m_builder->read_buffer(m_pass, std::move(read_info), access);
}

auto RgPassBuilder::write_buffer(RgBufferWriteInfo &&write_info,
                                 const RgBufferAccessDesc &access)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  return m_builder->write_buffer(m_pass, std::move(write_info), access);
}

auto RgPassBuilder::create_uniform_buffer(RgBufferCreateInfo &&create_info)
    -> RgRtBuffer {
  create_info.heap = BufferHeap::Upload;
  return std::get<1>(
      create_buffer(std::move(create_info),
                    {
                        .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                    }));
}

auto RgPassBuilder::create_compute_buffer(RgBufferCreateInfo &&create_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  create_info.heap = BufferHeap::Device;
  return create_buffer(std::move(create_info),
                       {
                           .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                       });
}

auto RgPassBuilder::create_transfer_buffer(RgBufferCreateInfo &&create_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  create_info.heap = BufferHeap::Device;
  return create_buffer(std::move(create_info),
                       {
                           .stage_mask = 0,
                           .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       });
}

auto RgPassBuilder::create_upload_buffer(RgBufferCreateInfo &&create_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  create_info.heap = BufferHeap::Upload;
  return create_buffer(std::move(create_info), {
                                                   .stage_mask = 0,
                                                   .access_mask = 0,
                                               });
}

auto RgPassBuilder::read_indirect_buffer(RgBufferReadInfo &&read_info)
    -> RgRtBuffer {
  return read_buffer(std::move(read_info),
                     {
                         .stage_mask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                         .access_mask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                     });
}

auto RgPassBuilder::read_index_buffer(RgBufferReadInfo &&read_info)
    -> RgRtBuffer {
  return read_buffer(std::move(read_info),
                     {
                         .stage_mask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
                         .access_mask = VK_ACCESS_2_INDEX_READ_BIT,
                     });
}

auto RgPassBuilder::read_vertex_shader_buffer(RgBufferReadInfo &&read_info)
    -> RgRtBuffer {
  return read_buffer(std::move(read_info),
                     {
                         .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                         .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                     });
}

auto RgPassBuilder::read_fragment_shader_buffer(RgBufferReadInfo &&read_info)
    -> RgRtBuffer {
  return read_buffer(std::move(read_info),
                     {
                         .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                     });
}

auto RgPassBuilder::read_compute_buffer(RgBufferReadInfo &&read_info)
    -> RgRtBuffer {
  return read_buffer(std::move(read_info),
                     {
                         .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                     });
}

auto RgPassBuilder::read_transfer_buffer(RgBufferReadInfo &&read_info)
    -> RgRtBuffer {
  return read_buffer(std::move(read_info),
                     {
                         .stage_mask = 0,
                         .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
                     });
}

auto RgPassBuilder::read_storage_texture(RgTextureReadInfo &&read_info)
    -> RgRtTexture {
  return read_texture(std::move(read_info),
                      {
                          .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                          .layout = VK_IMAGE_LAYOUT_GENERAL,
                      });
}

auto RgPassBuilder::write_compute_buffer(RgBufferWriteInfo &&write_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  return write_buffer(std::move(write_info),
                      {
                          .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                      });
}

auto RgPassBuilder::write_transfer_buffer(RgBufferWriteInfo &&write_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  return write_buffer(std::move(write_info),
                      {
                          .stage_mask = 0,
                          .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      });
}

auto RgPassBuilder::create_texture(RgTextureCreateInfo &&create_info,
                                   const RgTextureAccessDesc &access)
    -> std::tuple<RgTexture, RgRtTexture> {
  return m_builder->create_texture(m_pass, std::move(create_info), access);
}

auto RgPassBuilder::read_texture(RgTextureReadInfo &&read_info,
                                 const RgTextureAccessDesc &access)
    -> RgRtTexture {
  return m_builder->read_texture(m_pass, std::move(read_info), access);
}

auto RgPassBuilder::write_texture(RgTextureWriteInfo &&write_info,
                                  const RgTextureAccessDesc &access)
    -> std::tuple<RgTexture, RgRtTexture> {
  return m_builder->write_texture(m_pass, std::move(write_info), access);
}

auto RgPassBuilder::create_storage_texture(RgTextureCreateInfo &&create_info)
    -> std::tuple<RgTexture, RgRtTexture> {
  return create_texture(
      std::move(create_info),
      {
          .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .layout = VK_IMAGE_LAYOUT_GENERAL,
      });
}

auto RgPassBuilder::create_transfer_texture(RgTextureCreateInfo &&create_info)
    -> std::tuple<RgTexture, RgRtTexture> {
  return create_texture(std::move(create_info),
                        {
                            .stage_mask = 0,
                            .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        });
}

auto RgPassBuilder::read_sampled_texture(RgTextureReadInfo &&read_info)
    -> RgRtTexture {
  return read_texture(std::move(read_info),
                      {
                          .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          .access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                          .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                      });
}

auto RgPassBuilder::read_transfer_texture(RgTextureReadInfo &&read_info)
    -> RgRtTexture {
  return read_texture(std::move(read_info),
                      {
                          .stage_mask = 0,
                          .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
                          .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      });
}

auto RgPassBuilder::write_storage_texture(RgTextureWriteInfo &&write_info)
    -> std::tuple<RgTexture, RgRtTexture> {
  return write_texture(std::move(write_info),
                       {
                           .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                           .layout = VK_IMAGE_LAYOUT_GENERAL,
                       });
}

auto RgPassBuilder::write_transfer_texture(RgTextureWriteInfo &&write_info)
    -> std::tuple<RgTexture, RgRtTexture> {
  return write_texture(std::move(write_info),
                       {
                           .stage_mask = 0,
                           .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       });
}

#if 0
void RgPassBuilder::set_color_attachment(RGColorAttachment attachment,
                                         u32 index) {
  auto &attachments = m_render_pass.color_attachments;
  if (attachments.size() <= index) {
    attachments.resize(index + 1);
  }
  attachments[index] = std::move(attachment);
}

auto RgPassBuilder::write_color_attachment(
    RGTextureWriteInfo &&write_info,
    RGColorAttachmentWriteInfo &&attachment_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  write_info.stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  write_info.access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  write_info.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
  auto texture = write_texture(std::move(write_info));
  set_color_attachment(
      {
          .texture = texture,
          .ops =
              {
                  .load = VK_ATTACHMENT_LOAD_OP_LOAD,
                  .store = VK_ATTACHMENT_STORE_OP_STORE,
              },
      },
      attachment_info.index);
  return texture;
}

auto RgPassBuilder::create_color_attachment(
    RGTextureCreateInfo &&create_info,
    RGColorAttachmentCreateInfo &&attachment_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  create_info.stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  create_info.access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  create_info.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
  auto texture = create_texture(std::move(create_info));
  auto clear_color = attachment_info.ops.get<RGAttachmentClearColor>().map(
      [](const RGAttachmentClearColor &clear) { return clear.color; });
  set_color_attachment(
      {
          .texture = texture,
          .ops =
              {
                  .load = clear_color ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                      : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .store = VK_ATTACHMENT_STORE_OP_STORE,
                  .clear_color = clear_color.value_or(glm::vec4(0.0f)),
              },
      },
      attachment_info.index);
  return texture;
}

void RgPassBuilder::read_depth_attachment(
    RGTextureReadInfo &&read_info,
    RGDepthAttachmentReadInfo &&attachment_info) {
  read_info.stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
  read_info.access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  read_info.layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
  read_texture(std::move(read_info));
  m_render_pass.depth_attachment = RGDepthAttachment{
      .texture = read_info.texture,
      .depth_ops =
          DepthAttachmentOperations{
              .load = VK_ATTACHMENT_LOAD_OP_LOAD,
              .store = VK_ATTACHMENT_STORE_OP_NONE,
          },
  };
}

auto RgPassBuilder::write_depth_attachment(
    RGTextureWriteInfo &&write_info,
    RGDepthAttachmentWriteInfo &&attachment_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  write_info.stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  write_info.access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  write_info.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
  auto texture = write_texture(std::move(write_info));
  m_render_pass.depth_attachment = RGDepthAttachment{
      .texture = texture,
      .depth_ops =
          DepthAttachmentOperations{
              .load = VK_ATTACHMENT_LOAD_OP_LOAD,
              .store = VK_ATTACHMENT_STORE_OP_STORE,
          },
  };
  return texture;
}

auto RgPassBuilder::create_depth_attachment(
    RGTextureCreateInfo &&create_info,
    RGDepthAttachmentCreateInfo &&attachment_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  create_info.type = VK_IMAGE_TYPE_2D;
  create_info.stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  create_info.access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  create_info.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
  auto texture = create_texture(std::move(create_info));
  m_render_pass.depth_attachment = RGDepthAttachment{
      .texture = texture,
      .depth_ops =
          DepthAttachmentOperations{
              .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .store = VK_ATTACHMENT_STORE_OP_STORE,
              .clear_depth = attachment_info.depth_ops.depth,
          },
  };
  return texture;
}
#endif

#if 0
void RgPassBuilder::set_host_callback(RGHostPassCallback cb) {
  assert(cb);
  m_builder->set_callback(
      m_pass, [cb = std::move(cb)](Device &device, RGRuntime &rg,
                                   CommandRecorder &) { cb(device, rg); });
}

void RgPassBuilder::set_graphics_callback(RGGraphicsPassCallback cb) {
  assert(cb);
  m_builder->set_callback(m_pass, [cb = std::move(cb),
                                   begin_info = std::move(m_render_pass)](
                                      Device &device, RGRuntime &rg,
                                      CommandRecorder &cmd) {
    auto color_attachments =
        begin_info.color_attachments |
        map([&](const Optional<RGColorAttachment> &attachment)
                -> Optional<ColorAttachment> {
          return attachment.map(
              [&](const RGColorAttachment &attachment) -> ColorAttachment {
                return {
                    .texture = rg.get_texture(attachment.texture),
                    .ops = attachment.ops,
                };
              });
        }) |
        ranges::to<
            StaticVector<Optional<ColorAttachment>, MAX_COLOR_ATTACHMENTS>>;
    auto render_pass = cmd.render_pass({
        .color_attachments = color_attachments,
        .depth_stencil_attachment = begin_info.depth_attachment.map(
            [&](const RGDepthAttachment &attachment) -> DepthStencilAttachment {
              return {
                  .texture = rg.get_texture(attachment.texture),
                  .depth_ops = attachment.depth_ops,
              };
            }),
    });
    cb(device, rg, render_pass);
  });
}

void RgPassBuilder::set_compute_callback(RGComputePassCallback cb) {
  assert(cb);
  m_builder->set_callback(m_pass,
                          [cb = std::move(cb)](Device &device, RGRuntime &rg,
                                               CommandRecorder &cmd) {
                            auto pass = cmd.compute_pass();
                            cb(device, rg, pass);
                          });
}

void RgPassBuilder::set_transfer_callback(RGTransferPassCallback cb) {
  assert(cb);
  m_builder->set_callback(m_pass, std::move(cb));
}
#endif

} // namespace ren
