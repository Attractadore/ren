#pragma once
#include "Descriptors.hpp"

#include <span>

namespace ren {

auto get_set_layout_bindings(std::span<const std::byte> code)
    -> StaticVector<std::array<DescriptorBinding, MAX_DESCIPTOR_BINDINGS>,
                    MAX_DESCRIPTOR_SETS>;

} // namespace ren
