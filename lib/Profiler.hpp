#pragma once

#if REN_PROFILER
#include <tracy/Tracy.hpp>
#endif

namespace ren::prof {

inline void mark_frame() {
#if REN_PROFILER
  FrameMark;
#endif
}

#if REN_PROFILER

#define ren_prof_zone(name) ZoneScopedN(name)
#define ren_prof_zone_text(text) ZoneText(text.data(), text.size())

#else

#define ren_prof_zone(name)
#define ren_prof_zone_text(text)

#endif

} // namespace ren::prof
