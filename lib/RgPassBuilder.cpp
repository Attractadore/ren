#include "RenderGraph.hpp"

namespace ren {

auto RgPassBuilder::create_buffer(RgBufferCreateInfo &&create_info,
                                  const RgBufferUsage &usage) -> RgRtBuffer {
  String name = std::move(create_info.name);
  String init_name = fmt::format("rg-init-{}", name);
  create_info.name = init_name;
  m_builder->create_buffer(std::move(create_info));
  return write_buffer(std::move(name), init_name, usage);
}

auto RgPassBuilder::read_buffer(StringView buffer, const RgBufferUsage &usage,
                                u32 temporal_layer) -> RgRtBuffer {
  return m_builder->read_buffer(m_pass, buffer, usage, temporal_layer);
}

auto RgPassBuilder::write_buffer(String dst_buffer, StringView src_buffer,
                                 const RgBufferUsage &usage) -> RgRtBuffer {
  return m_builder->write_buffer(m_pass, std::move(dst_buffer), src_buffer,
                                 usage);
}

auto RgPassBuilder::create_texture(RgTextureCreateInfo &&create_info,
                                   const RgTextureUsage &usage) -> RgRtTexture {
  todo();
};

auto RgPassBuilder::read_texture(StringView texture,
                                 const RgTextureUsage &usage,
                                 u32 temporal_layer) -> RgRtTexture {
  todo();
}

auto RgPassBuilder::write_texture(String dst_texture, StringView src_texture,
                                  const RgTextureUsage &usage) -> RgRtTexture {
  todo();
}

auto RgPassBuilder::create_color_attachment(
    RgTextureCreateInfo &&create_info, const ColorAttachmentOperations &ops)
    -> RgRtTexture {
  todo();
}

auto RgPassBuilder::create_depth_attachment(
    RgTextureCreateInfo &&create_info, const DepthAttachmentOperations &ops)
    -> RgRtTexture {
  todo();
}

auto RgPassBuilder::write_color_attachment(StringView dst_texture,
                                           StringView src_texture,
                                           const ColorAttachmentOperations &ops)
    -> RgRtTexture {
  todo();
}

auto RgPassBuilder::write_depth_attachment(StringView dst_texture,
                                           StringView src_texture,
                                           const DepthAttachmentOperations &ops)
    -> RgRtTexture {
  todo();
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
