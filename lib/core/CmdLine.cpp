#include "ren/core/CmdLine.hpp"
#include "ren/core/Format.hpp"

#include <cinttypes>
#include <cstdio>

namespace ren {

namespace {

const CmdLineOption *parse_opt_name(const char *arg,
                                    Span<const CmdLineOption> options,
                                    const CmdLineOption *positional) {
  if (arg[0] == '-') {
    if (arg[1] == 0) {
      return nullptr;
    }

    // Long option
    if (arg[1] == '-') {
      for (const CmdLineOption &opt : options) {
        if (opt.name == &arg[2]) {
          return &opt;
        }
      }
      return nullptr;
    }

    if (arg[2] != 0) {
      return nullptr;
    }

    // Short option
    for (const CmdLineOption &opt : options) {
      if (opt.short_name == arg[1]) {
        return &opt;
      }
    }
    return nullptr;
  }

  return positional;
}

bool parse_opt_value(const char *arg, CmdLineOption opt,
                     NotNull<ParsedCmdLineOption *> parsed) {
  *parsed = {};
  switch (opt.type) {
  default:
    return false;
  case CmdLineOptionType::Int:
    if (std::sscanf(arg, "%" PRIi64, &parsed->as_int) != 1) {
      return false;
    }
    break;
  case CmdLineOptionType::UInt:
    if (std::sscanf(arg, "%" PRIu64, &parsed->as_int) != 1) {
      return false;
    }
    break;
  case CmdLineOptionType::String:
    parsed->as_string = String8::init(arg);
    break;
  }
  parsed->is_set = true;
  return true;
}

} // namespace

bool parse_cmd_line(const char *argv[], Span<const CmdLineOption> options,
                    Span<ParsedCmdLineOption> parsed) {
  ren_assert(options.size() == parsed.size());

  ScratchArena scratch;

  DynamicArray<CmdLineOption> positional_opts;
  for (CmdLineOption opt : options) {
    if (opt.category == CmdLineOptionCategory::Positional) {
      ren_assert(opt.type != CmdLineOptionType::Flag);
      positional_opts.push(scratch, opt);
    }
  }
  usize positional_index = 0;

  argv++;
  while (argv[0]) {
    const char *arg = argv[0];
    if (arg[0] == '\0') {
      continue;
    }

    const CmdLineOption *positional_opt =
        positional_index < positional_opts.m_size
            ? &positional_opts[positional_index]
            : nullptr;
    const CmdLineOption *opt = parse_opt_name(argv[0], options, positional_opt);
    if (!opt) {
      return false;
    }
    if (opt == positional_opt) {
      positional_index++;
    } else {
      argv++;
    }

    if (opt->type == CmdLineOptionType::Flag) {
      parsed[opt->tag] = {.is_set = true};
      continue;
    }

    if (!argv[0]) {
      return false;
    }
    if (!parse_opt_value(argv[0], *opt, &parsed[opt->tag])) {
      return false;
    }
    argv++;
  }

  for (CmdLineOption opt : options) {
    if (opt.category != CmdLineOptionCategory::Optional and
        not parsed[opt.tag].is_set) {
      return false;
    }
  }
  ren_assert(positional_index == positional_opts.m_size);

  return true;
}

String8 cmd_line_help(NotNull<Arena *> arena, const char *argv_0,
                      Span<const CmdLineOption> options, usize tab_size,
                      usize width) {
  ScratchArena scratch(arena);

  auto positional_args = StringBuilder8::init(scratch);
  for (CmdLineOption opt : options) {
    if (opt.category != CmdLineOptionCategory::Positional) {
      continue;
    }
    if (positional_args.m_buffer.m_size > 0) {
      positional_args.push(' ');
    }
    positional_args.push(opt.name);
  }

  auto str = StringBuilder8::init(scratch);
  format_to(&str, "Usage: {} [OPTIONS]... {}\n", argv_0, positional_args);

  for (CmdLineOption opt : options) {
    usize offset = str.m_buffer.m_size;
    for (auto _ : range(tab_size)) {
      str.push(' ');
    }
    if (opt.short_name) {
      str.push('-');
      str.push(opt.short_name);
      str.push(',');
    } else {
      str.push("   ");
    }
    str.push(' ');
    if (opt.category != CmdLineOptionCategory::Positional) {
      str.push("--");
    }
    str.push(opt.name);
    switch (opt.type) {
    default:
      break;
    case CmdLineOptionType::Int:
      str.push(" int");
      break;
    case CmdLineOptionType::UInt:
      str.push(" uint");
      break;
    case CmdLineOptionType::String:
      str.push(" string");
      break;
    }
    usize line_width = str.m_buffer.m_size - offset;
    if (line_width >= width) {
      str.push('\n');
      for (auto _ : range(width)) {
        str.push(' ');
      }
    } else {
      for (auto _ : range(line_width, width)) {
        str.push(' ');
      }
    }
    str.push(opt.help);
    str.push('\n');
  }

  return str.materialize(arena);
}

} // namespace ren
