#include "RenderGraph.hpp"

namespace ren {

RgPassBuilder::RgPassBuilder(RgPassId pass, RgBuilder &builder) {
  m_pass = pass;
  m_builder = &builder;
}

auto RgPassBuilder::create_buffer(RgBufferCreateInfo &&create_info,
                                  const RgBufferUsage &usage) -> RgBufferId {
  String name = std::move(create_info.name);
  String init_name = fmt::format("rg-init-{}", name);
  create_info.name = init_name;
  m_builder->create_buffer(std::move(create_info));
  return write_buffer(name, init_name, usage);
}

auto RgPassBuilder::read_buffer(StringView buffer, const RgBufferUsage &usage)
    -> RgBufferId {
  return m_builder->read_buffer(m_pass, buffer, usage);
}

auto RgPassBuilder::write_buffer(StringView dst_buffer, StringView src_buffer,
                                 const RgBufferUsage &usage) -> RgBufferId {
  return m_builder->write_buffer(m_pass, std::move(dst_buffer), src_buffer,
                                 usage);
}

auto RgPassBuilder::create_texture(RgTextureCreateInfo &&create_info,
                                   const RgTextureUsage &usage) -> RgTextureId {
  String name = std::move(create_info.name);
  String init_name = fmt::format("rg-init-{}", name);
  create_info.name = init_name;
  m_builder->create_texture(std::move(create_info));
  return write_texture(name, init_name, usage);
};

auto RgPassBuilder::read_texture(StringView texture,
                                 const RgTextureUsage &usage,
                                 u32 temporal_layer) -> RgTextureId {
  return m_builder->read_texture(m_pass, texture, usage, temporal_layer);
}

auto RgPassBuilder::write_texture(StringView dst_texture,
                                  StringView src_texture,
                                  const RgTextureUsage &usage) -> RgTextureId {
  return m_builder->write_texture(m_pass, dst_texture, src_texture, usage);
}

void RgPassBuilder::add_color_attachment(u32 index, RgTextureId texture,
                                         const ColorAttachmentOperations &ops) {
  auto &color_attachments = m_builder->m_passes[m_pass]
                                .type.get_or_emplace<RgGraphicsPassInfo>()
                                .color_attachments;
  if (color_attachments.size() <= index) {
    color_attachments.resize(index + 1);
  }
  color_attachments[index] = {
      .texture = texture,
      .ops = ops,
  };
}

void RgPassBuilder::add_depth_attachment(RgTextureId texture,
                                         const DepthAttachmentOperations &ops) {
  m_builder->m_passes[m_pass]
      .type.get_or_emplace<RgGraphicsPassInfo>()
      .depth_stencil_attachment = RgDepthStencilAttachment{
      .texture = texture,
      .depth_ops = ops,
  };
}

auto RgPassBuilder::create_color_attachment(
    RgTextureCreateInfo &&create_info, const ColorAttachmentOperations &ops,
    u32 index) -> RgTextureId {
  RgTextureId texture =
      create_texture(std::move(create_info), RG_COLOR_ATTACHMENT);
  add_color_attachment(index, texture, ops);
  return texture;
}

auto RgPassBuilder::write_color_attachment(StringView dst_texture,
                                           StringView src_texture,
                                           const ColorAttachmentOperations &ops,
                                           u32 index) -> RgTextureId {
  RgTextureId texture =
      write_texture(dst_texture, src_texture, RG_COLOR_ATTACHMENT);
  add_color_attachment(index, texture, ops);
  return texture;
}

auto RgPassBuilder::create_depth_attachment(
    RgTextureCreateInfo &&create_info, const DepthAttachmentOperations &ops)
    -> RgTextureId {
  RgTextureId texture =
      create_texture(std::move(create_info), RG_READ_WRITE_DEPTH_ATTACHMENT);
  add_depth_attachment(texture, ops);
  return texture;
}

auto RgPassBuilder::read_depth_attachment(StringView src_texture)
    -> RgTextureId {
  RgTextureId texture = read_texture(src_texture, RG_READ_DEPTH_ATTACHMENT);
  add_depth_attachment(texture, {
                                    .load = VK_ATTACHMENT_LOAD_OP_LOAD,
                                    .store = VK_ATTACHMENT_STORE_OP_NONE,
                                });
  return texture;
}

auto RgPassBuilder::write_depth_attachment(StringView dst_texture,
                                           StringView src_texture,
                                           const DepthAttachmentOperations &ops)
    -> RgTextureId {
  RgTextureId texture =
      write_texture(dst_texture, src_texture, RG_READ_WRITE_DEPTH_ATTACHMENT);
  add_depth_attachment(texture, ops);
  return texture;
}

void RgPassBuilder::wait_semaphore(RgSemaphoreId semaphore,
                                   VkPipelineStageFlags2 stages, u64 value) {
  m_builder->wait_semaphore(m_pass, semaphore, stages, value);
}

void RgPassBuilder::signal_semaphore(RgSemaphoreId semaphore,
                                     VkPipelineStageFlags2 stages, u64 value) {
  m_builder->signal_semaphore(m_pass, semaphore, stages, value);
}

} // namespace ren
