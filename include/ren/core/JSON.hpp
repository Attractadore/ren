#pragma once
#include "Result.hpp"
#include "String.hpp"

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
  [[nodiscard]] static JsonValue init(String8 string);
  [[nodiscard]] static JsonValue init(NotNull<Arena *> arena, String8 string);
  [[nodiscard]] static JsonValue init(i64 integer);
  [[nodiscard]] static JsonValue init(double number);
  [[nodiscard]] static JsonValue init(bool boolean);

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

inline String8 json_string_value_or(JsonValue object, String8 key,
                                    String8 default_value) {
  for (JsonKeyValue kv : json_object(object)) {
    if (kv.key == key) {
      if (kv.value.type == JsonType::String) {
        return json_string(kv.value);
      }
      break;
    }
  }
  return default_value;
}

inline i64 json_integer_value(JsonValue object, String8 key) {
  return json_integer(json_value(object, key));
}

inline i64 json_integer_value_or(JsonValue object, String8 key,
                                 i64 default_val) {
  JsonValue val = json_value(object, key);
  if (val.type == JsonType::Integer) {
    return json_integer(val);
  }
  return default_val;
}

inline bool json_bool_value_or(JsonValue object, String8 key,
                               bool default_val) {
  JsonValue val = json_value(object, key);
  if (val.type == JsonType::Boolean) {
    return val.boolean;
  }
  return default_val;
}

} // namespace ren
