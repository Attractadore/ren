#include "AssetLoader.hpp"

#include <fmt/format.h>
#include <fmt/std.h>

#include <fstream>

namespace ren {
void AssetLoader::add_search_directory(std::filesystem::path path) {
  m_search_directories.emplace_back(std::move(path));
}

void AssetLoader::load_file(const std::filesystem::path &path,
                            Vector<std::byte> &out) const {
  if (!try_load_file(path, out)) {
    throw std::runtime_error(fmt::format("Failed to open {}", path));
  }
}

bool AssetLoader::try_load_file(const std::filesystem::path &path,
                                Vector<std::byte> &out) const {
  constexpr auto mode = std::ios::binary;

  auto read = [&](std::ifstream &file, const std::filesystem::path &path) {
    out.resize(std::filesystem::file_size(path));
    file.read(reinterpret_cast<char *>(out.data()), out.size());
    if (!file) {
      throw std::runtime_error(fmt::format("Failed to read from {}", path));
    }
  };

  std::ifstream file;
  if (path.is_absolute()) {
    file.open(path, mode);
    if (file) {
      read(file, path);
      return true;
    }
  } else {
    for (const auto &dir : m_search_directories) {
      auto open_path = dir / path;
      file.open(open_path, mode);
      if (file) {
        read(file, open_path);
        return true;
      }
    }
  }

  return false;
}
} // namespace ren
