#include "ren/core/Algorithm.hpp"
#include "ren/core/Assert.hpp"
#include "ren/core/CmdLine.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/Span.hpp"
#include "ren/core/String.hpp"

#include <spirv/unified1/spirv.h>
#include <utility>

namespace ren {

enum class ShaderStage {
  Unknown,
  Vertex,
  Fragment,
  Compute,
};

struct CompileOptions {
  Path src;
  Path spv;
  Path project_src_dir;
};

struct Member {
  String8 type;
  String8 name;
};

} // namespace ren

using namespace ren;

namespace {

auto get_file_shader_stage(Path ext) -> ShaderStage {
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

  Path shader_header;
  {
    IoResult<Path> result = opts.src.absolute(scratch);
    if (!result) {
      fmt::println(stderr, "Failed to get absolute path for {}: {}", opts.src,
                   result.m_status);
      return EXIT_FAILURE;
    }
    shader_header = result.m_value.replace_extension(scratch, Path::init(".h"));
  }
  {
    IoResult<bool> result = shader_header.exists();
    if (!result or !result.m_value) {
      fmt::println(stderr, "{} does not exist: {}", shader_header,
                   result.m_status);
      return EXIT_FAILURE;
    }
  }

  Path project_src_dir;
  {
    IoResult<Path> result = opts.project_src_dir.absolute(scratch);
    if (!result) {
      fmt::println(stderr, "Failed to get absolute path for {}: {}", opts.src,
                   result.m_status);
      return EXIT_FAILURE;
    }
    project_src_dir = result.m_value;
  }

  Path hpp_dst = opts.spv.replace_extension(scratch, Path::init(".hpp"));
  Path cpp_dst = opts.spv.replace_extension(scratch, Path::init(".cpp"));

  Span<const u32> spirv;
  {
    IoResult<Span<char>> buffer = read(scratch, opts.spv);
    if (!buffer) {
      fmt::println(stderr, "Failed to read {}:", opts.spv, buffer.m_status);
      return EXIT_FAILURE;
    }
    spirv = {(const u32 *)buffer.m_value.m_data,
             buffer.m_value.size_bytes() / 4};
  }

  // TODO: Verify
  ren_assert(spirv[0] == SpvMagicNumber);
  u32 spv_bound = spirv[3];

  // Parse all structs.
  auto struct_names = Span<String8>::allocate(scratch, spv_bound);
  auto struct_member_counts = Span<u32>::allocate(scratch, spv_bound);
  auto struct_member_offsets = Span<u32>::allocate(scratch, spv_bound);
  auto id_def_words = Span<u32>::allocate(scratch, spv_bound);
  u32 pc_type = -1;
  for (usize word = 5; word < spirv.m_size;) {
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
  exclusive_scan(struct_member_counts, struct_member_offsets.m_data, 0);
  usize num_members =
      struct_member_offsets.back() + struct_member_counts.back();

  // Parse all struct members.
  auto member_names = Span<String8>::allocate(scratch, num_members);
  auto member_offsets = Span<u32>::allocate(scratch, num_members);
  for (usize word = 5; word < spirv.m_size;) {
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
  format_to(&header, "#pragma once\n#include \"{}\"\n\n",
            shader_header.native(scratch));

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

    format_to(&header, "// {}\n", name);
    for (usize m : range(struct_member_counts[i])) {
      format_to(&header, "static_assert(offsetof(::ren::sh::{}, {}) == {});\n",
                name, member_names[offset + m], member_offsets[offset + m]);
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
      format_to(&member_type, "decltype(::ren::sh::{}::{})", pc_name,
                member_name);

      format_to(&member_declarations, "  ::ren::RgPushConstant<{}> {};\n",
                member_type, member_name);
      format_to(&member_conversions,
                "    .{0} = rg.to_push_constant<{1}>(from.{0}),\n", member_name,
                member_type);
    }

    format_to(&header,
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
              project_src_dir.native(scratch));
  }

  auto binary_variable_name = StringBuilder8::init(scratch);
  format_to(&binary_variable_name, "{}{}", opts.src.stem(),
            get_stage_short_name(stage));

  format_to(&header,
            R"(#include <cstddef>
#include <cstdint>

namespace ren {{

extern const uint32_t {}[];
extern const size_t {}Size;

}})",
            binary_variable_name, binary_variable_name);

  StringBuilder spirv_str(scratch);
  format_to(&spirv_str, "{:#010x}", spirv[0]);
  for (usize i : range<usize>(1, spirv.m_size)) {
    format_to(&spirv_str, ",\n  {:#010x}", spirv[i]);
  }

  format_to(&source, R"(#include <cstddef>
#include <cstdint>

namespace ren {{

const extern uint32_t {0}[] = {{
  {1}
}};
const extern size_t {0}Size = sizeof({0}) / sizeof(uint32_t);
}}
)",
            binary_variable_name, spirv_str);

  if (IoStatus status =
          write(hpp_dst, header.m_buffer.m_data, header.m_buffer.m_size);
      status != IoSuccess) {
    fmt::println(stderr, "Failed write {}: {}", hpp_dst, status);
    return EXIT_FAILURE;
  }
  if (IoStatus status =
          write(cpp_dst, source.m_buffer.m_data, source.m_buffer.m_size);
      status != IoSuccess) {
    fmt::println(stderr, "Failed write {}: {}", cpp_dst, status);
    return EXIT_FAILURE;
  }

  return 0;
}

} // namespace

enum ProcessShaderOptions {
  OPTION_HELP,
  OPTION_FILE,
  OPTION_SRC,
  OPTION_PROJECT,
  OPTION_COUNT,
};

int main(int argc, const char *argv[]) {
  ScratchArena::init_allocator();
  ScratchArena scratch;

  // clang-format off
  CmdLineOption options[] = {
      {OPTION_FILE, CmdLinePath, "file", 0, "path to SPIR-V file", CmdLinePositional},
      {OPTION_SRC, CmdLinePath, "src", 0, "path to GLSL source file", CmdLineRequired},
      {OPTION_PROJECT, CmdLinePath, "project-src-dir", 0, "value of PROJECT_SOURCE_DIR", CmdLineRequired},
      {OPTION_HELP, CmdLineFlag, "help", 'h', "show this message"},
  };
  // clang-format on
  ParsedCmdLineOption parsed[OPTION_COUNT];
  bool success = parse_cmd_line(scratch, argv, options, parsed);
  if (!success or parsed[OPTION_HELP].is_set) {
    fmt::print("{}", cmd_line_help(scratch, argv[0], options));
    return EXIT_FAILURE;
  }

  return process({
      .src = parsed[OPTION_SRC].as_path,
      .spv = parsed[OPTION_FILE].as_path,
      .project_src_dir = parsed[OPTION_PROJECT].as_path,
  });
}
