#include "core/Assert.hpp"
#include "core/IO.hpp"
#include "core/Span.hpp"
#include "core/String.hpp"
#include "core/Vector.hpp"
#include "core/Views.hpp"

#include <SPIRV/GlslangToSpv.h>
#include <cxxopts.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <numeric>
#include <spirv_reflect.h>

namespace fs = std::filesystem;

namespace ren {

enum class ShaderStage {
  Unknown,
  Vertex,
  Fragment,
  Compute,
};

struct CompileOptions {
  fs::path project_src_dir;
  fs::path src;
  fs::path dst_dir;
  fs::path deps;
  bool debug = false;
};

struct Member {
  String type;
  String name;
};

} // namespace ren

using namespace ren;

namespace {

void read_file(const fs::path &p, Vector<char> &buffer) {
  buffer.resize(fs::file_size(p));
  std::ifstream f(p, std::ios::binary);
  f.read(buffer.data(), buffer.size());
}

auto get_file_shader_stage(const fs::path &p) -> ShaderStage {
  using enum ShaderStage;
  fs::path ext = p.extension();
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

auto gen_asserts(const spv_reflect::ShaderModule &sm) -> String {
  Span<const uint32_t> spirv(sm.GetCode(), sm.GetCodeSize() / 4);
  ren_assert(spirv[0] == SpvMagicNumber);

  uint32_t spv_bound = spirv[3];

  // Parse all structs.
  Vector<StringView> struct_names(spv_bound);
  Vector<u32> struct_member_counts(spv_bound);
  Vector<u32> struct_member_offsets(spv_bound);
  for (usize word = 5; word < spirv.size();) {
    usize num_words = spirv[word] >> SpvWordCountShift;
    SpvOp op = SpvOp(spirv[word] & SpvOpCodeMask);

    if (op == SpvOpName) {
      u32 structure = spirv[word + 1];
      const char *name = (const char *)&spirv[word + 2];
      struct_names[structure] = name;
    } else if (op == SpvOpTypeStruct) {
      u32 structure = spirv[word + 1];
      struct_member_counts[structure] = num_words - 2;
    }

    word += num_words;
  }
  usize last_struct_member_count = struct_member_counts.back();
  std::exclusive_scan(struct_member_counts.begin(), struct_member_counts.end(),
                      struct_member_offsets.begin(), 0);
  usize num_members = last_struct_member_count + struct_member_offsets.back();

  // Parse all struct members.
  Vector<StringView> member_names(num_members);
  Vector<u32> member_offsets(num_members);
  for (usize word = 5; word < spirv.size();) {
    usize num_words = spirv[word] >> SpvWordCountShift;
    SpvOp op = SpvOp(spirv[word] & SpvOpCodeMask);

    if (op == SpvOpMemberName) {
      u32 structure = spirv[word + 1];
      u32 member = spirv[word + 2];
      const char *name = (const char *)&spirv[word + 3];
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

  String result = "#include <cstddef>\n\n";
  for (usize i : range(spv_bound)) {
    if (struct_names[i].empty() or struct_member_counts[i] == 0) {
      continue;
    }

    StringView name = struct_names[i];
    if (name.ends_with("_natural")) {
      name.remove_suffix(std::strlen("_natural"));
    }

    usize offset = struct_member_offsets[i];

    bool any_offset_non_zero = false;
    for (usize m : range(struct_member_counts[i])) {
      any_offset_non_zero =
          any_offset_non_zero or member_offsets[offset + m] != 0;
    }
    if (not any_offset_non_zero) {
      continue;
    }

    result += fmt::format("// {}\n", struct_names[i]);
    for (usize m : range(struct_member_counts[i])) {
      result += fmt::format(
          "static_assert(offsetof(::ren::glsl::{}, {}) == {});\n", name,
          member_names[offset + m], member_offsets[offset + m]);
    }
    result += '\n';
  }

  return result;
}

auto gen_rg_args(const CompileOptions &opts,
                 const spv_reflect::ShaderModule &sm, String &hpp) -> int {
  fs::path shader_header = fs::absolute(opts.src).replace_extension(".h");
  if (not fs::exists(shader_header)) {
    fmt::println(stderr, "{} does not exist", shader_header);
    return -1;
  }

  String asserts_hpp = gen_asserts(sm);

  SpvReflectResult result = SPV_REFLECT_RESULT_SUCCESS;

  const SpvReflectBlockVariable *pc = sm.GetPushConstantBlock(0, &result);
  if (result) {
    fmt::println(stderr, "Failed to get push constant block: {}", (int)result);
    return -1;
  }
  if (StringView(pc->name) != "pc") {
    fmt::println(stderr, "Unknown push constants name: {}", pc->name);
    return -1;
  }

  const SpvReflectTypeDescription *type = pc->type_description;

  Vector<Member> members(type->member_count);
  String element_type;
  for (usize i : range(type->member_count)) {
    element_type.clear();
    fmt::format_to(std::back_inserter(element_type),
                   "decltype(::ren::glsl::{}::{})", type->type_name,
                   type->members[i].struct_member_name);
    Member &member = members[i];
    member.type = element_type;
    member.name = type->members[i].struct_member_name;
  }

  constexpr StringView RAW_PREFIX = "raw_";

  String member_declarations;
  for (const Member &member : members) {
    StringView member_name = member.name;
    if (member_name.starts_with(RAW_PREFIX)) {
      member_name.remove_prefix(RAW_PREFIX.size());
      member_declarations +=
          fmt::format("  {} {};\n", member.type, member_name);
    } else {
      member_declarations += fmt::format("  ::ren::RgPushConstant<{}> {};\n",
                                         member.type, member_name);
    }
  }

  String member_conversions;
  for (const Member &member : members) {
    if (member.name.starts_with(RAW_PREFIX)) {
      StringView member_name = member.name;
      member_name.remove_prefix(RAW_PREFIX.size());
      member_conversions +=
          fmt::format("    .{0} = from.{1},\n", member.name, member_name);
    } else {
      member_conversions +=
          fmt::format("    .{0} = rg.to_push_constant<{1}>(from.{0}),\n",
                      member.name, member.type);
    }
  }

  String rg_hpp = fmt::format(
      R"(#ifndef Rg{0}_DEFINED
#define Rg{0}_DEFINED

#include "{3}/lib/RenderGraph.hpp"

#include <cstdint>

namespace ren {{

struct Rg{0} {{
{1}}};

inline auto to_push_constants(const ::ren::RgRuntime& rg, const Rg{0}& from) -> ::ren::glsl::{0} {{
  return {{
{2}  }};
}}

}}

#endif // Rg{0}_DEFINED)",
      type->type_name, member_declarations, member_conversions,
      to_system_path(fs::absolute(opts.project_src_dir)));

  hpp.clear();
  fmt::format_to(std::back_inserter(hpp),
                 R"(#include "{}"

{}{})",
                 to_system_path(shader_header), asserts_hpp, rg_hpp);

  return 0;
}

auto glslang_compile(const CompileOptions &opts) -> int {
  ShaderStage stage = get_file_shader_stage(opts.src);
  if (stage == ShaderStage::Unknown) {
    fmt::println(stderr, "Unknown shader stage for input file {}", opts.src);
    return -1;
  }

  if (not fs::exists(opts.src)) {
    fmt::println(stderr, "Shader source file not found: {}", opts.src);
    return -1;
  }

  EShLanguage language = [&]() {
    switch (stage) {
    default:
      std::unreachable();
    case ShaderStage::Vertex:
      return EShLangVertex;
    case ShaderStage::Fragment:
      return EShLangFragment;
    case ShaderStage::Compute:
      return EShLangCompute;
    }
  }();

  glslang::TShader shader(language);
  shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan,
                     100);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
  shader.setOverrideVersion(460);
  shader.setPreamble("#extension GL_GOOGLE_include_directive : require\n");
  shader.setDebugInfo(opts.debug);

  Vector<char> src;
  read_file(opts.src, src);
  const char *src_str = src.data();
  int src_size = src.size();
  String src_name = opts.src.string();
  const char *src_name_cstr = src_name.c_str();
  shader.setStringsWithLengthsAndNames(&src_str, &src_size, &src_name_cstr, 1);

  struct Includer : glslang::TShader::Includer {
    virtual ~Includer() = default;

    using IncludeResult = glslang::TShader::Includer::IncludeResult;

    struct Include {
      IncludeResult result;
      Vector<char> buffer;
    };
    static_assert(offsetof(Include, result) == 0);

    virtual auto includeLocal(const char *f, const char *, size_t depth)
        -> IncludeResult * override {
      fs::path path = root / f;
      if (not fs::exists(path)) {
        return nullptr;
      }
      Vector<char> buffer;
      read_file(path, buffer);
      if (StringView(buffer).starts_with("#pragma once")) {
        if (std::ranges::find(m_included_files, path) !=
            m_included_files.end()) {
          buffer.clear();
        } else {
          buffer.erase(buffer.begin(),
                       buffer.begin() + StringView("#pragma once").size());
        }
      }
      auto *result = (IncludeResult *)new Include{
          .result = IncludeResult(path.string(), buffer.data(), buffer.size(),
                                  nullptr),
          .buffer = std::move(buffer),
      };
      m_included_files.push_back(path);
      return result;
    }

    virtual void releaseInclude(IncludeResult *result) override {
      delete ((Include *)result);
    }

    fs::path root;
    Vector<fs::path> m_included_files;
  } includer;

  includer.root = opts.src.parent_path();

  EShMessages messages = EShMsgDefault;
  if (opts.debug) {
    messages = EShMsgDebugInfo;
  }

  if (!shader.parse(GetDefaultResources(), 100, false, messages, includer)) {
    fmt::println(stderr, "Compilation failed:\n{}", shader.getInfoLog());
    return -1;
  } else if (shader.getInfoLog() and std::strlen(shader.getInfoLog()) > 0) {
    fmt::println(stderr, "{}", shader.getInfoLog());
  }

  glslang::TProgram program;
  program.addShader(&shader);
  if (!program.link(messages)) {
    fmt::println(stderr, "Linking failed:\n{}", program.getInfoLog());
  } else if (program.getInfoLog() and std::strlen(program.getInfoLog()) > 0) {
    fmt::println(stderr, "{}", program.getInfoLog());
  }

  glslang::SpvOptions spv_opts;
  if (opts.debug) {
    spv_opts.generateDebugInfo = true;
    spv_opts.emitNonSemanticShaderDebugInfo = true;
    spv_opts.emitNonSemanticShaderDebugSource = true;
  }

  std::vector<unsigned int> spirv;
  glslang::GlslangToSpv(*program.getIntermediate(language), spirv, &spv_opts);

  spv_reflect::ShaderModule sm(spirv, SPV_REFLECT_MODULE_FLAG_NO_COPY);

  String rg_hpp;
  if (int ret = gen_rg_args(opts, sm, rg_hpp)) {
    return ret;
  }

  String asserts_hpp = gen_asserts(sm);

  String var_name =
      fmt::format("{}{}", opts.src.stem(), get_stage_short_name(stage));

  fs::create_directory(opts.dst_dir);
  fs::path base_dst = opts.dst_dir / opts.src.filename();
  base_dst += ".glsl";
  fs::path spv_dst = base_dst.replace_extension(".spv");
  fs::path hpp_dst = base_dst.replace_extension(".hpp");
  fs::path cpp_dst = base_dst.replace_extension(".cpp");

  {
    std::ofstream f(spv_dst, std::ios::binary);
    if (!f) {
      fmt::println("Failed to open {} for writing", spv_dst);
    }
    f.write((const char *)spirv.data(), Span(spirv).size_bytes());
  }

  {
    String header = fmt::format(
        R"(#pragma once

#include <cstddef>
#include <cstdint>

namespace ren {{

extern const uint32_t {}[];
extern const size_t {}Size;

}}

{})",
        var_name, var_name, rg_hpp);
    std::ofstream f(hpp_dst, std::ios::binary);
    if (!f) {
      fmt::println("Failed to open {} for writing", hpp_dst);
      return -1;
    }
    f.write(header.data(), header.size());
  }

  {
    String code = fmt::format("{:#010x}", fmt::join(spirv, ",\n  "));
    String source = fmt::format(R"(#include <cstddef>
#include <cstdint>

namespace ren {{

const extern uint32_t {}[] = {{
  {}
}};
const extern size_t {}Size = sizeof({}) / sizeof(uint32_t);
}})",
                                var_name, code, var_name, var_name);
    std::ofstream f(cpp_dst, std::ios::binary);
    if (!f) {
      fmt::println("Failed to open {} for writing", hpp_dst);
      return -1;
    }
    f.write(source.data(), source.size());
  }

  if (not opts.deps.empty()) {
    Vector<fs::path> products = {spv_dst, hpp_dst, cpp_dst};

    Vector<fs::path> deps = std::move(includer.m_included_files);
    deps.push_back(opts.src);
    for (fs::path &p : deps) {
      p = fs::absolute(p);
    }
    std::ranges::sort(deps);
    auto r = std::ranges::unique(deps);
    deps.erase(r.begin(), r.end());

    String dep_file = fmt::format(
        "{}: {}",
        fmt::join(std::views::transform(products, to_system_path), " "),
        fmt::join(std::views::transform(deps, to_system_path), " "));

    fs::path dep_dir = opts.deps.parent_path();
    if (not dep_dir.empty()) {
      fs::create_directory(dep_dir);
    }
    std::ofstream f(opts.deps, std::ios::binary);
    if (!f) {
      fmt::println("Failed to open {} for writing", opts.deps);
      return -1;
    }
    f.write(dep_file.data(), dep_file.size());
  }

  return 0;
}

} // namespace

int main(int argc, const char *argv[]) {
  CompileOptions opts;
  cxxopts::Options cmd_opts("shader-compiler", "ren shader compiler tool");
  // clang-format off
  cmd_opts.add_options()
      ("file", "path to GLSL source file", cxxopts::value<fs::path>())
      ("o,output-dir", "output directory", cxxopts::value<fs::path>())
      ("g", "generate debug info")
      ("depfile", "write dependency file", cxxopts::value<fs::path>())
      ("project-src-dir", "value of PROJECT_SOURCE_DIR", cxxopts::value<fs::path>())
      ("h,help", "show this message")
  ;
  // clang-format on
  cmd_opts.parse_positional({"file"});
  cmd_opts.positional_help("file");
  cxxopts::ParseResult result = cmd_opts.parse(argc, argv);
  if (result.count("help") or !result.count("file") or
      !result.count("output-dir") or !result.count("project-src-dir")) {
    fmt::println("{}", cmd_opts.help());
    return -1;
  }

  opts.project_src_dir = result["project-src-dir"].as<fs::path>();
  opts.src = result["file"].as<fs::path>();
  opts.dst_dir = result["output-dir"].as<fs::path>();
  if (result.count("depfile")) {
    opts.deps = result["depfile"].as<fs::path>();
  }
  if (result.count("g")) {
    opts.debug = true;
  }

  if (!glslang::InitializeProcess()) {
    fmt::println(stderr, "Failed to initalize glslang");
    return -1;
  }
  int res = glslang_compile(opts);
  glslang::FinalizeProcess();

  return res;
}
