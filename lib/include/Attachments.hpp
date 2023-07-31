#pragma once
#include "Support/StdDef.hpp"

namespace ren {

struct ClearDepthStencil {
  float depth = 0.0f;
  u32 stencil = 0;
};

struct ColorAttachmentOperations {
  VkAttachmentLoadOp load = VK_ATTACHMENT_LOAD_OP_CLEAR;
  VkAttachmentStoreOp store = VK_ATTACHMENT_STORE_OP_STORE;
  glm::vec4 clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthAttachmentOperations {
  VkAttachmentLoadOp load = VK_ATTACHMENT_LOAD_OP_CLEAR;
  VkAttachmentStoreOp store = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  float clear_depth = 0.0f;
};

struct StencilAttachmentOperations {
  VkAttachmentLoadOp load = VK_ATTACHMENT_LOAD_OP_LOAD;
  VkAttachmentStoreOp store = VK_ATTACHMENT_STORE_OP_STORE;
  u32 clear_stencil = 0;
};

} // namespace ren
