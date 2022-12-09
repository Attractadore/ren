#pragma once
#include <fmt/format.h>

#include "Formats.hpp"
#include "Texture.hpp"

template <> struct fmt::formatter<ren::RenderTargetViewDesc> {
  template <typename ParseContext> constexpr auto parse(ParseContext &ctx) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(ren::RenderTargetViewDesc const &rtv_desc, FormatContext &ctx) {
    return fmt::format_to(ctx.out(), "{{ format: {}, level: {}, layer: {} }}",
                          to_string(rtv_desc.format), rtv_desc.level,
                          rtv_desc.layer);
  }
};
