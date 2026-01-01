#pragma once
#include "Result.hpp"
#include "String.hpp"
#include "ren/core/Optional.hpp"

namespace ren {

enum class JsonError {
  Unknown,
  InvalidSyntax,
  InvalidCodeUnit,
  EndOfFile,
};

String8 format_as(JsonError error);

enum class JsonType {
  Null,
  Object,
  Array,
  String,
  Integer,
  Number,
  Boolean,
};

String8 format_as(JsonType type);

struct JsonValue;
struct JsonKeyValue;

struct JsonValue {
  JsonType type = JsonType::Null;
  union {
    Span<const JsonKeyValue> object = {};
    Span<const JsonValue> array;
    String8 string;
    i64 integer;
    double number;
    bool boolean;
  };

public:
  [[nodiscard]] static JsonValue init(Span<const JsonKeyValue> object);
  [[nodiscard]] static JsonValue init(Span<const JsonValue> array);
  [[nodiscard]] static JsonValue from_string(String8 string);
  [[nodiscard]] static JsonValue from_string(NotNull<Arena *> arena,
                                             String8 string);
  [[nodiscard]] static JsonValue from_integer(i64 integer);
  [[nodiscard]] static JsonValue from_float(double number);
  [[nodiscard]] static JsonValue from_boolean(bool boolean);

  explicit operator bool() const { return type != JsonType::Null; }
};

struct JsonKeyValue {
  String8 key;
  JsonValue value;
};

struct JsonErrorInfo {
  JsonError error = JsonError::Unknown;
  usize offset = 0;
  // 1-based.
  usize line = 0;
  // 1-based.
  usize column = 0;
};

[[nodiscard]] Result<JsonValue, JsonErrorInfo>
json_parse(NotNull<Arena *> arena, String8 buffer);

String8 json_serialize(NotNull<Arena *> arena, JsonValue json);

template <JsonType Type> auto json_try_cast(JsonValue json) {
  if constexpr (Type == JsonType::Object) {
    return json.type == JsonType::Object ? Optional(json.object) : NullOpt;
  } else if constexpr (Type == JsonType::Array) {
    return json.type == JsonType::Array ? Optional(json.array) : NullOpt;
  } else if constexpr (Type == JsonType::String) {
    return json.type == JsonType::String ? Optional(json.string) : NullOpt;
  } else if constexpr (Type == JsonType::Integer) {
    return json.type == JsonType::Integer ? Optional(json.integer) : NullOpt;
  } else if constexpr (Type == JsonType::Number) {
    if (json.type == JsonType::Number) {
      return Optional<double>(json.number);
    } else if (json.type == JsonType::Integer) {
      return Optional<double>(json.integer);
    } else {
      return Optional<double>();
    }
  } else if constexpr (Type == JsonType::Boolean) {
    return json.type == JsonType::Boolean ? Optional(json.boolean) : NullOpt;
  } else {
    return;
  }
}

template <JsonType Type> auto json_cast(JsonValue json) {
  return *json_try_cast<Type>(json);
}

inline Span<const JsonKeyValue> json_object(JsonValue value) {
  ren_assert(value.type == JsonType::Object);
  return value.object;
}

inline Span<const JsonValue> json_array(JsonValue value) {
  ren_assert(value.type == JsonType::Array);
  return value.array;
}

inline String8 json_string(JsonValue value) {
  ren_assert(value.type == JsonType::String);
  return value.string;
}

inline i64 json_integer(JsonValue value) {
  ren_assert(value.type == JsonType::Integer);
  return value.integer;
}

inline JsonValue json_value(JsonValue object, String8 key) {
  for (JsonKeyValue kv : json_object(object)) {
    if (kv.key == key) {
      return kv.value;
    }
  }
  return {};
}

inline Span<const JsonValue> json_array_value(JsonValue object, String8 key) {
  return json_array(json_value(object, key));
}

inline String8 json_string_value(JsonValue object, String8 key) {
  return json_string(json_value(object, key));
}

inline i64 json_integer_value(JsonValue object, String8 key) {
  return json_integer(json_value(object, key));
}

inline i64 json_integer_value_or(JsonValue object, String8 key,
                                 i64 default_value) {
  for (JsonKeyValue kv : json_object(object)) {
    if (kv.key == key and kv.value.type == JsonType::Integer) {
      return kv.value.integer;
    }
  }
  return default_value;
}

} // namespace ren
