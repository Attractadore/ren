#include "core/IO.hpp"
#include "core/Span.hpp"
#include "core/Vector.hpp"
#include "core/Views.hpp"
#include "ren/core/Assert.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/String.hpp"

#include <cxxopts.hpp>
#include <filesystem>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <numeric>
#include <spirv/unified1/spirv.h>

namespace fs = std::filesystem;

namespace ren {

enum class ShaderStage {
  Unknown,
  Vertex,
  Fragment,
  Compute,
};

struct CompileOptions {
  fs::path src;
  fs::path spv;
  fs::path project_src_dir;
};

struct Member {
  String8 type;
  String8 name;
};

} // namespace ren

using namespace ren;

namespace {

void read_file(const fs::path &p, Vector<char> &buffer) {
  buffer.resize(fs::file_size(p));
  std::ifstream f(p, std::ios::binary);
  f.read(buffer.data(), buffer.size());
}

auto get_file_shader_stage(const fs::path &ext) -> ShaderStage {
  using enum ShaderStage;
  if (ext == ".vert") {
    return Vertex;
  } else if (ext == ".frag") {
    return Fragment;
  } else if (ext == ".comp") {
    return Compute;
  }
  return Unknown;
}

auto get_stage_short_name(ShaderStage stage) -> const char * {
  using enum ShaderStage;
  ren_assert(stage != Unknown);
  switch (stage) {
  default:
    std::unreachable();
  case ShaderStage::Vertex:
    return "VS";
  case ShaderStage::Fragment:
    return "FS";
  case ShaderStage::Compute:
    return "CS";
  }
}

auto process(const CompileOptions &opts) -> int {
  ScratchArena scratch;

  ShaderStage stage = get_file_shader_stage(opts.src.extension());
  if (stage == ShaderStage::Unknown) {
    fmt::println(stderr, "Unknown shader stage for input file {}", opts.src);
    return -1;
  }

  fs::path shader_header = fs::absolute(opts.src).replace_extension(".h");
  if (not fs::exists(shader_header)) {
    fmt::println(stderr, "{} does not exist", shader_header);
    return -1;
  }

  fs::path hpp_dst = fs::path(opts.spv).replace_extension(".hpp");
  fs::path cpp_dst = fs::path(opts.spv).replace_extension(".cpp");

  Vector<char> buffer;
  buffer.resize(fs::file_size(opts.spv));
  FILE *f = fopen(opts.spv, "rb");
  if (!f) {
    fmt::println(stderr, "Failed to open {} for reading", opts.spv);
    return -1;
  }
  usize num_read = std::fread(buffer.data(), 1, buffer.size(), f);
  std::fclose(f);
  if (num_read != buffer.size()) {
    fmt::println(stderr, "Failed to read from {}", opts.spv);
    return -1;
  }

  Span<const u32> spirv((const u32 *)buffer.data(), buffer.size() / 4);
  // TODO: Verify
  ren_assert(spirv[0] == SpvMagicNumber);
  u32 spv_bound = spirv[3];

  // Parse all structs.
  Vector<String8> struct_names(spv_bound);
  Vector<u32> struct_member_counts(spv_bound);
  Vector<u32> struct_member_offsets(spv_bound);
  Vector<u32> id_def_words(spv_bound);
  u32 pc_type = -1;
  for (usize word = 5; word < spirv.size();) {
    usize num_words = spirv[word] >> SpvWordCountShift;
    SpvOp op = SpvOp(spirv[word] & SpvOpCodeMask);

    if (op == SpvOpName) {
      u32 structure = spirv[word + 1];
      auto name = String8::init((const char *)&spirv[word + 2]);
      if (name.starts_with("ren.sh.")) {
        name = name.remove_prefix(std::strlen("ren.sh."));
      }
      if (name.ends_with("_natural")) {
        name = name.remove_suffix(std::strlen("_natural"));
      }
      struct_names[structure] = name;
    } else if (op == SpvOpTypeStruct) {
      u32 structure = spirv[word + 1];
      struct_member_counts[structure] = num_words - 2;
    } else if (op == SpvOpTypePointer) {
      u32 id = spirv[word + 1];
      id_def_words[id] = word;
    } else if (op == SpvOpVariable) {
      u32 type_pointer = spirv[word + 1];
      u32 storage_class = spirv[word + 3];
      if (storage_class == SpvStorageClassPushConstant) {
        ren_assert(pc_type == -1);
        u32 tp_word = id_def_words[type_pointer];
        ren_assert(tp_word != 0);
        u32 tp_storage_class = spirv[tp_word + 2];
        ren_assert(tp_storage_class == SpvStorageClassPushConstant);
        u32 tp_type = spirv[tp_word + 3];
        pc_type = tp_type;
        ren_assert(struct_names[pc_type].m_size > 0);
      }
    }

    word += num_words;
  }
  std::exclusive_scan(struct_member_counts.begin(), struct_member_counts.end(),
                      struct_member_offsets.begin(), 0);
  usize num_members =
      struct_member_offsets.back() + struct_member_counts.back();

  // Parse all struct members.
  Vector<String8> member_names(num_members);
  Vector<u32> member_offsets(num_members);
  for (usize word = 5; word < spirv.size();) {
    usize num_words = spirv[word] >> SpvWordCountShift;
    SpvOp op = SpvOp(spirv[word] & SpvOpCodeMask);

    if (op == SpvOpMemberName) {
      u32 structure = spirv[word + 1];
      u32 member = spirv[word + 2];
      auto name = String8::init((const char *)&spirv[word + 3]);
      member_names[struct_member_offsets[structure] + member] = name;
    } else if (op == SpvOpMemberDecorate) {
      u32 structure = spirv[word + 1];
      u32 member = spirv[word + 2];
      SpvDecoration decoration = SpvDecoration(spirv[word + 3]);
      if (decoration == SpvDecorationOffset) {
        u32 offset = spirv[word + 4];
        member_offsets[struct_member_offsets[structure] + member] = offset;
      }
    }

    word += num_words;
  }

  auto header = StringBuilder8::init(scratch, 128 * 1024);
  auto source = StringBuilder8::init(scratch, 128 * 1024);
  fmt::format_to(header.back_inserter(), "#pragma once\n#include \"{}\"\n\n",
                 to_system_path(scratch, shader_header));

  // Generate static asserts for struct fields.
  header.push("#include <cstddef>\n\n");
  for (usize i : range(spv_bound)) {
    if (struct_names[i].m_size == 0 or struct_member_counts[i] == 0) {
      continue;
    }

    String8 name = struct_names[i];

    usize offset = struct_member_offsets[i];

    bool any_offset_non_zero = false;
    for (usize m : range(struct_member_counts[i])) {
      any_offset_non_zero =
          any_offset_non_zero or member_offsets[offset + m] != 0;
    }
    if (not any_offset_non_zero) {
      continue;
    }

    fmt::format_to(header.back_inserter(), "// {}\n", name);
    for (usize m : range(struct_member_counts[i])) {
      fmt::format_to(header.back_inserter(),
                     "static_assert(offsetof(::ren::sh::{}, {}) == {});\n",
                     name, member_names[offset + m],
                     member_offsets[offset + m]);
    }
    header.push('\n');
  }

  // Generate render graph binding code.
  if (pc_type == -1) {
    fmt::println(stderr, "Failed to find push constant block in {}", opts.spv);
  } else {
    String8 pc_name = struct_names[pc_type];

    auto member_declarations = StringBuilder8::init(scratch);
    auto member_conversions = StringBuilder8::init(scratch);
    usize offset = struct_member_offsets[pc_type];
    for (usize i : range(struct_member_counts[pc_type])) {
      String8 member_name = member_names[offset + i];

      auto member_type = StringBuilder8::init(scratch);
      fmt::format_to(member_type.back_inserter(), "decltype(::ren::sh::{}::{})",
                     pc_name, member_name);

      fmt::format_to(member_declarations.back_inserter(),
                     "  ::ren::RgPushConstant<{}> {};\n", member_type,
                     member_name);
      fmt::format_to(member_conversions.back_inserter(),
                     "    .{0} = rg.to_push_constant<{1}>(from.{0}),\n",
                     member_name, member_type);
    }

    fmt::format_to(header.back_inserter(),
                   R"(#ifndef Rg{0}_DEFINED
#define Rg{0}_DEFINED

#include "{3}/lib/RenderGraph.hpp"

namespace ren {{

struct Rg{0} {{
{1}}};

inline auto to_push_constants(const ::ren::RgRuntime& rg, const Rg{0}& from) -> ::ren::sh::{0} {{
  return {{
{2}  }};
}}

}}

#endif // Rg{0}_DEFINED

)",
                   pc_name, member_declarations, member_conversions,
                   to_system_path(scratch, fs::absolute(opts.project_src_dir)));
  }

  auto binary_variable_name = StringBuilder8::init(scratch);
  fmt::format_to(binary_variable_name.back_inserter(), "{}{}", opts.src.stem(),
                 get_stage_short_name(stage));

  fmt::format_to(header.back_inserter(),
                 R"(#include <cstddef>
#include <cstdint>

namespace ren {{

extern const uint32_t {}[];
extern const size_t {}Size;

}})",
                 binary_variable_name, binary_variable_name);

  fmt::format_to(source.back_inserter(), R"(#include <cstddef>
#include <cstdint>

namespace ren {{

const extern uint32_t {0}[] = {{
  {1:#010x}
}};
const extern size_t {0}Size = sizeof({0}) / sizeof(uint32_t);
}}
)",
                 binary_variable_name, fmt::join(spirv, ",\n  "));

  {
    std::ofstream f(hpp_dst, std::ios::binary);
    if (!f) {
      fmt::println("Failed to open {} for writing", hpp_dst);
      return -1;
    }
    f.write(header.m_buffer.m_data, header.m_buffer.m_size);
  }

  {
    std::ofstream f(cpp_dst, std::ios::binary);
    if (!f) {
      fmt::println("Failed to open {} for writing", hpp_dst);
      return -1;
    }
    f.write(source.m_buffer.m_data, source.m_buffer.m_size);
  }

  return 0;
}

} // namespace

int main(int argc, const char *argv[]) {
  CompileOptions opts;
  cxxopts::Options cmd_opts("shader-compiler", "ren shader compiler tool");
  // clang-format off
  cmd_opts.add_options()
      ("file", "path to SPIR-V file", cxxopts::value<fs::path>())
      ("src", "path to GLSL source file", cxxopts::value<fs::path>())
      ("project-src-dir", "value of PROJECT_SOURCE_DIR", cxxopts::value<fs::path>())
      ("h,help", "show this message")
  ;
  // clang-format on
  cmd_opts.parse_positional({"file"});
  cmd_opts.positional_help("file");
  cxxopts::ParseResult result = cmd_opts.parse(argc, argv);
  if (result.count("help") or !result.count("src") or !result.count("file") or
      !result.count("project-src-dir")) {
    fmt::println("{}", cmd_opts.help());
    return -1;
  }

  opts.project_src_dir = result["project-src-dir"].as<fs::path>();
  opts.spv = result["file"].as<fs::path>();
  opts.src = result["src"].as<fs::path>();

  ScratchArena::init_allocator();
  return process(opts);
}
