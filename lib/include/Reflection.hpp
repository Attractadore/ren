#pragma once
#include "Descriptors.hpp"

#include <span>

namespace ren {

auto get_set_layout_descs(std::span<const std::byte> code)
    -> StaticVector<DescriptorSetLayoutDesc, MAX_DESCIPTOR_SETS>;

} // namespace ren
