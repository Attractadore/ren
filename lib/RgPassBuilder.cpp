#include "RenderGraph.hpp"

namespace ren {

RgPassBuilder::RgPassBuilder(RgPassId pass, RgBuilder &builder) {
  m_pass = pass;
  m_builder = &builder;
}

auto RgPassBuilder::read_buffer(RgBufferId buffer,
                                const RgBufferUsage &usage) -> RgBufferToken {
  return m_builder->read_buffer(m_pass, buffer, usage);
}

auto RgPassBuilder::write_buffer(RgDebugName name, RgBufferId buffer,
                                 const RgBufferUsage &usage)
    -> std::tuple<RgBufferId, RgBufferToken> {
  return m_builder->write_buffer(m_pass, std::move(name), buffer, usage);
}

auto RgPassBuilder::read_texture(RgTextureId texture,
                                 const RgTextureUsage &usage,
                                 u32 temporal_layer) -> RgTextureToken {
  return m_builder->read_texture(m_pass, texture, usage, temporal_layer);
}

auto RgPassBuilder::write_texture(RgDebugName name, RgTextureId texture,
                                  const RgTextureUsage &usage)
    -> std::tuple<RgTextureId, RgTextureToken> {
  return m_builder->write_texture(m_pass, std::move(name), texture, usage);
}

void RgPassBuilder::add_color_attachment(u32 index, RgTextureToken texture,
                                         const ColorAttachmentOperations &ops) {
  auto &color_attachments = m_builder->m_data->m_passes[m_pass]
                                .data.get_or_emplace<RgGraphicsPassInfo>()
                                .color_attachments;
  if (color_attachments.size() <= index) {
    color_attachments.resize(index + 1);
  }
  color_attachments[index] = {
      .texture = texture,
      .ops = ops,
  };
}

void RgPassBuilder::add_depth_attachment(RgTextureToken texture,
                                         const DepthAttachmentOperations &ops) {
  m_builder->m_data->m_passes[m_pass]
      .data.get_or_emplace<RgGraphicsPassInfo>()
      .depth_stencil_attachment = RgDepthStencilAttachment{
      .texture = texture,
      .depth_ops = ops,
  };
}

auto RgPassBuilder::write_color_attachment(
    RgDebugName name, RgTextureId texture, const ColorAttachmentOperations &ops,
    u32 index) -> std::tuple<RgTextureId, RgTextureToken> {
  auto [new_texture, token] =
      write_texture(std::move(name), texture, RG_COLOR_ATTACHMENT);
  add_color_attachment(index, token, ops);
  return {new_texture, token};
}

auto RgPassBuilder::read_depth_attachment(
    RgTextureId texture, u32 temporal_layer) -> RgTextureToken {
  RgTextureToken token =
      read_texture(texture, RG_READ_DEPTH_ATTACHMENT, temporal_layer);
  add_depth_attachment(token, {
                                  .load = VK_ATTACHMENT_LOAD_OP_LOAD,
                                  .store = VK_ATTACHMENT_STORE_OP_NONE,
                              });
  return token;
}

auto RgPassBuilder::write_depth_attachment(RgDebugName name,
                                           RgTextureId texture,
                                           const DepthAttachmentOperations &ops)
    -> std::tuple<RgTextureId, RgTextureToken> {
  auto [new_texture, token] =
      write_texture(std::move(name), texture, RG_READ_WRITE_DEPTH_ATTACHMENT);
  add_depth_attachment(token, ops);
  return {new_texture, token};
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
