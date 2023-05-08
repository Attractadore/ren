#include "ResourceArena.hpp"
#include "Device.hpp"

namespace ren::detail {

template <typename... Ts>
void ResourceArenaImpl<Ts...>::clear(Device &device) {}

} // namespace ren::detail
