#pragma once
#include <memory>

namespace ren {

template <typename H>
using SharedHandle = std::shared_ptr<std::remove_pointer_t<H>>;

}
