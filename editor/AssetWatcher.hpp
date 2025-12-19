#pragma once
#include "ren/core/NotNull.hpp"

namespace ren {

struct EditorContext;

void start_asset_watcher(NotNull<EditorContext *> ctx);

void stop_asset_watcher(NotNull<EditorContext *> ctx);

void run_asset_watcher(NotNull<EditorContext *> ctx);

} // namespace ren
