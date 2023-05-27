#pragma once
#include <fmt/ostream.h>
#include <iostream>

namespace ren {
enum class LogCategory {
  general,
  rendergraph,
  vk,
};

enum class LogSeverity {
  error,
  warn,
  info,
  debug,
};

namespace detail {
inline std::ostream &getLogStream(LogCategory, LogSeverity severity) {
  using enum LogSeverity;
  switch (severity) {
  case error:
  case warn:
    return std::cerr;
  case info:
  case debug:
    return std::cout;
  }
}

template <LogCategory> std::string_view category_name = "";
#define DEFINE_CATEGORY_NAME(C)                                                \
  template <>                                                                  \
  constexpr inline std::string_view category_name<LogCategory::C> = #C

DEFINE_CATEGORY_NAME(general);
DEFINE_CATEGORY_NAME(vk);
DEFINE_CATEGORY_NAME(rendergraph);

#undef CATEGORY_NAME

template <LogSeverity> std::string_view severity_name = "";
#define DEFINE_SEVERITY_NAME(S)                                                \
  template <>                                                                  \
  constexpr inline std::string_view severity_name<LogSeverity::S> = #S

DEFINE_SEVERITY_NAME(error);
DEFINE_SEVERITY_NAME(warn);
DEFINE_SEVERITY_NAME(info);
DEFINE_SEVERITY_NAME(debug);

#undef SEVERITY_NAME

template <LogCategory> constexpr bool enable_category = true;

template <LogSeverity> constexpr bool enable_severity = false;
#define ENABLE_SEVERITY(S)                                                     \
  template <> constexpr inline bool enable_severity<LogSeverity::S> = true

#if REN_LOG_ERROR
ENABLE_SEVERITY(error);
#endif
#if REN_LOG_WARN
ENABLE_SEVERITY(warn);
#endif
#if REN_LOG_INFO
ENABLE_SEVERITY(info);
#endif
#if REN_LOG_DEBUG
ENABLE_SEVERITY(debug);
#endif

#undef ENABLE_SEVERITY

template <LogCategory C, LogSeverity S, typename... T>
inline void logSeverity(fmt::format_string<T...> fmt_str, T &&...args) {
  if constexpr (not enable_severity<S>) {
    return;
  }
  auto &s = getLogStream(C, S);
  if constexpr (category_name<C>.empty()) {
    fmt::print(s, "{}: ", severity_name<S>);
  } else {
    fmt::print(s, "{}/{}: ", severity_name<S>, category_name<C>);
  }
  fmt::print(s, std::move(fmt_str), std::forward<T>(args)...);
  fmt::print(s, "\n");
}

template <LogCategory C, typename... T>
inline void logCategory(LogSeverity severity, fmt::format_string<T...> fmt_str,
                        T &&...args) {
  if constexpr (not enable_category<C>) {
    return;
  }
  using enum LogSeverity;
#define LOG_SEVERITY(S)                                                        \
  case S: {                                                                    \
    logSeverity<C, S>(std::move(fmt_str), std::forward<T>(args)...);           \
    break;                                                                     \
  }
  switch (severity) {
    LOG_SEVERITY(error);
    LOG_SEVERITY(warn);
    LOG_SEVERITY(info);
    LOG_SEVERITY(debug);
  };
}
#undef LOG_SEVERITY
} // namespace detail

template <typename... T>
void log(LogCategory category, LogSeverity severity,
         fmt::format_string<T...> fmt_str, T &&...args) {
  using enum LogCategory;
#define LOG_CATEGORY(C)                                                        \
  case C: {                                                                    \
    detail::logCategory<C>(severity, std::move(fmt_str),                       \
                           std::forward<T>(args)...);                          \
    break;                                                                     \
  }
  switch (category) {
    LOG_CATEGORY(general);
    LOG_CATEGORY(rendergraph);
    LOG_CATEGORY(vk);
  }
#undef LOG_CATEGORY
}

#define DEFINE_LOG(name, category)                                             \
  template <typename... T>                                                     \
  void name(LogSeverity severity, fmt::format_string<T...> fmt_str,            \
            T &&...args) {                                                     \
    log(LogCategory::category, severity, std::move(fmt_str),                   \
        std::forward<T>(args)...);                                             \
  }

#define DEFINE_LOG_SEVERITY(name, category, severity)                          \
  template <typename... T>                                                     \
  void name(fmt::format_string<T...> fmt_str, T &&...args) {                   \
    log(LogCategory::category, LogSeverity::severity, std::move(fmt_str),      \
        std::forward<T>(args)...);                                             \
  }

#define DEFINE_LOG_ERROR(name, category)                                       \
  DEFINE_LOG_SEVERITY(name, category, error)
#define DEFINE_LOG_WARN(name, category)                                        \
  DEFINE_LOG_SEVERITY(name, category, warn)
#define DEFINE_LOG_INFO(name, category)                                        \
  DEFINE_LOG_SEVERITY(name, category, info)
#define DEFINE_LOG_DEBUG(name, category)                                       \
  DEFINE_LOG_SEVERITY(name, category, debug)

#define DEFINE_ALL_LOGS(category)                                              \
  DEFINE_LOG(category##Log, category);                                         \
  DEFINE_LOG_ERROR(category##Error, category);                                 \
  DEFINE_LOG_WARN(category##Warn, category);                                   \
  DEFINE_LOG_INFO(category##Log, category);                                    \
  DEFINE_LOG_DEBUG(category##Debug, category)

DEFINE_LOG(log, general);
DEFINE_LOG_ERROR(error, general);
DEFINE_LOG_WARN(warn, general);
DEFINE_LOG_INFO(log, general);
DEFINE_LOG_DEBUG(debug, general);

DEFINE_ALL_LOGS(vk);

DEFINE_ALL_LOGS(rendergraph);

#undef DEFINE_LOG
#undef DEFINE_LOG_SEVERITY
#undef DEFINE_LOG_ERROR
#undef DEFINE_LOG_WARN
#undef DEFINE_LOG_INFO
#undef DEFINE_LOG_DEBUG
} // namespace ren
