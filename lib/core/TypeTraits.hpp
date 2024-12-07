#pragma once
#include <type_traits>

namespace ren {

template <typename A, typename B>
using ConstLikeT = std::conditional_t<std::is_const_v<B>, const A, A>;

}
