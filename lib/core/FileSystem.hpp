#pragma once
#include "ren/core/FileSystem.hpp"

namespace ren {

IoResult<File> open_sync(Path path, FileAccessMode mode, FileOpenFlags flags);

IoResult<usize> read_sync(File file, void *buffer, usize size);

IoResult<usize> write_sync(File file, const void *buffer, usize size);

} // namespace ren
