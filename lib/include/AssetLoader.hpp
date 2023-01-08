#pragma once
#include "Support/Vector.hpp"

#include <filesystem>

namespace ren {

class AssetLoader {
  Vector<std::filesystem::path> m_search_directories;

public:
  void add_search_directory(std::filesystem::path path);

  void load_file(const std::filesystem::path &path,
                 Vector<std::byte> &out) const;
  [[nodiscard]] auto try_load_file(const std::filesystem::path &path,
                                   Vector<std::byte> &out) const -> bool;
};

} // namespace ren
