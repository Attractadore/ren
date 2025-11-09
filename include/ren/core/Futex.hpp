#pragma once
#include <atomic>

namespace ren {

void futex_wait(int* location, int value);

void futex_wake_one(int* location);

void futex_wake_all(int* location);

} // namespace ren
