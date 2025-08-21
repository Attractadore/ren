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
  String ns = "ren";
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

auto gen_rg_args(const CompileOptions &opts,
                 const spv_reflect::ShaderModule &sm, String &hpp, String &cpp)
    -> int {
  hpp.clear();
  cpp.clear();

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

  fs::path shader_header = fs::absolute(opts.src).replace_extension(".h");
  String shader_header_include;
  if (fs::exists(shader_header)) {
    shader_header_include =
        fmt::format(R"(#include "{}")", to_system_path(shader_header));
  }

  fmt::format_to(std::back_inserter(hpp), R"(
#ifndef RG_{1}_DEFINED
#define RG_{1}_DEFINED

#include "{4}/lib/RenderGraph.hpp"
{5}

#include <cstdint>

namespace {0} {{

struct Rg{1} {{
{2}}};

inline auto to_push_constants(const ::ren::RgRuntime& rg, const Rg{1}& from) -> ::ren::glsl::{1} {{
  return {{
{3}  }};
}}

}}

#endif // RG_{1}_DEFINED
)",
                 opts.ns, type->type_name, member_declarations,
                 member_conversions,
                 to_system_path(fs::absolute(opts.project_src_dir)),
                 shader_header_include);

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

  String rg_hpp, rg_cpp;
  if (int ret = gen_rg_args(opts, sm, rg_hpp, rg_cpp)) {
    return ret;
  }

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
    String header = fmt::format(R"(
#pragma once
#include <cstddef>
#include <cstdint>

namespace {} {{

extern const uint32_t {}[];
extern const size_t {}Size;

}}

{}
)",
                                opts.ns, var_name, var_name, rg_hpp);
    std::ofstream f(hpp_dst, std::ios::binary);
    if (!f) {
      fmt::println("Failed to open {} for writing", hpp_dst);
      return -1;
    }
    f.write(header.data(), header.size());
  }

  {
    String code = fmt::format("{:#010x}", fmt::join(spirv, ",\n  "));
    String source =
        fmt::format(R"(
#include <cstddef>
#include <cstdint>

namespace {} {{

const extern uint32_t {}[] = {{
  {}
}};
const extern size_t {}Size = sizeof({}) / sizeof(uint32_t);
}}

{}
)",
                    opts.ns, var_name, code, var_name, var_name, rg_cpp);
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
      ("namespace", "namespace to define variables in", cxxopts::value<String>()->default_value(opts.ns))
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
  opts.ns = result["namespace"].as<String>();

  if (!glslang::InitializeProcess()) {
    fmt::println(stderr, "Failed to initalize glslang");
    return -1;
  }
  int res = glslang_compile(opts);
  glslang::FinalizeProcess();

  return res;
}
