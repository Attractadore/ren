#pragma once
#include "Span.hpp"
#include "StdDef.hpp"
#include "String.hpp"

namespace ren {

enum class CmdLineOptionType {
  Flag,
  Int,
  UInt,
  String,
};
constexpr CmdLineOptionType CmdLineFlag = CmdLineOptionType::Flag;
constexpr CmdLineOptionType CmdLineInt = CmdLineOptionType::Int;
constexpr CmdLineOptionType CmdLineUInt = CmdLineOptionType::UInt;
constexpr CmdLineOptionType CmdLineString = CmdLineOptionType::String;

enum class CmdLineOptionCategory {
  Optional,
  Required,
  Positional,
};
constexpr CmdLineOptionCategory CmdLineOptional =
    CmdLineOptionCategory::Optional;
constexpr CmdLineOptionCategory CmdLineRequired =
    CmdLineOptionCategory::Required;
constexpr CmdLineOptionCategory CmdLinePositional =
    CmdLineOptionCategory::Positional;

struct CmdLineOption {
  i32 tag = -1;
  CmdLineOptionType type = {};
  String8 name;
  char short_name = 0;
  String8 help;
  CmdLineOptionCategory category = CmdLineOptionCategory::Optional;
};

struct ParsedCmdLineOption {
  bool is_set = false;
  union {
    i64 as_int;
    u64 as_uint;
    String8 as_string = {};
  };
};

bool parse_cmd_line(const char *argv[], Span<const CmdLineOption> options,
                    Span<ParsedCmdLineOption> parsed);

String8 cmd_line_help(NotNull<Arena *> arena, const char *argv_0,
                      Span<const CmdLineOption> options, usize tab_width = 2,
                      usize width = 40);

} // namespace ren
