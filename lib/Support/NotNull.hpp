#include <gsl/pointers>

namespace ren {

template <typename P>
  requires std::is_pointer_v<P>
using NotNull = gsl::not_null<P>;

}
