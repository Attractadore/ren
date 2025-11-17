#include "ren/core/JSON.hpp"
#include "ren/core/Algorithm.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/Unicode.hpp"

#include <cmath>
#include <utility>

namespace ren {

#define CASE_JSON_WHITESPACE                                                   \
  case '\n':                                                                   \
  case '\r':                                                                   \
  case ' ':                                                                    \
  case '\t'

#define JSON_EOF_ERROR                                                         \
  JsonErrorInfo { .error = JsonError::EndOfFile, .offset = ctx->i, }

#define JSON_SYNTAX_ERROR                                                      \
  JsonErrorInfo { .error = JsonError::InvalidSyntax, .offset = ctx->i, }

String8 format_as(JsonError error) {
  switch (error) {
  case JsonError::Unknown:
    return "Unknown";
  case JsonError::InvalidSyntax:
    return "Invalid syntax";
  case JsonError::InvalidCodeUnit:
    return "Invalid Unicode code unit";
  case JsonError::EndOfFile:
    return "Premature end of file";
  }
  std::unreachable();
}

JsonValue JsonValue::init(Span<const JsonKeyValue> object) {
  return {
      .type = JsonType::Object,
      .object = object,
  };
}

JsonValue JsonValue::init(Span<const JsonValue> array) {
  return {
      .type = JsonType::Array,
      .array = array,
  };
}

JsonValue JsonValue::init(String8 string) {
  return {
      .type = JsonType::String,
      .string = string,
  };
}

JsonValue JsonValue::init(NotNull<Arena *> arena, String8 string) {
  return JsonValue::init(string.copy(arena));
}

JsonValue JsonValue::init(i64 integer) {
  return {
      .type = JsonType::Integer,
      .integer = integer,
  };
}

struct JsonParserContext {
  Arena *arena;
  String8 buffer;
  usize i = 0;
};

Result<Utf16Char, JsonErrorInfo>
json_parse_utf16(NotNull<JsonParserContext *> ctx) {
  Utf16Char cu;
  for (auto _ : range(4)) {
    if (ctx->i == ctx->buffer.m_size) {
      return JSON_EOF_ERROR;
    }
    constexpr StackArray<u8, 256> MAP = []() {
      StackArray<u8, 256> map = {};
      fill(Span(map), 0xFF);
      map['0'] = 0;
      map['1'] = 1;
      map['2'] = 2;
      map['3'] = 3;
      map['4'] = 4;
      map['5'] = 5;
      map['6'] = 6;
      map['7'] = 7;
      map['8'] = 8;
      map['9'] = 9;
      map['a'] = 10;
      map['b'] = 11;
      map['c'] = 12;
      map['d'] = 13;
      map['e'] = 14;
      map['f'] = 15;
      map['A'] = 10;
      map['B'] = 11;
      map['C'] = 12;
      map['D'] = 13;
      map['E'] = 14;
      map['F'] = 15;
      return map;
    }();
    u8 digit = MAP[ctx->buffer[ctx->i]];
    if (digit == 0xFF) {
      return JSON_SYNTAX_ERROR;
    }
    ctx->i++;
    cu.value = (cu.value << 4) | digit;
  }
  return cu;
}

Result<String8, JsonErrorInfo>
json_parse_string(NotNull<JsonParserContext *> ctx) {
  ScratchArena scratch;
  auto builder = StringBuilder::init(scratch);
top:
  if (ctx->i == ctx->buffer.m_size) {
    return JSON_EOF_ERROR;
  }
  switch (ctx->buffer[ctx->i]) {
  case '"':
    ctx->i++;
    return builder.materialize(ctx->arena);
  case '\\': {
    ctx->i++;
    if (ctx->i == ctx->buffer.m_size) {
      return JSON_EOF_ERROR;
    }
    switch (ctx->buffer[ctx->i]) {
    case '"':
    case '\\':
    case '/':
      builder.push(ctx->buffer[ctx->i++]);
      goto top;
    case 'b':
    case 'f':
    case 'n':
    case 'r':
    case 't': {
      constexpr StackArray<char, 256> MAP = []() {
        StackArray<char, 256> map = {};
        map['b'] = '\b';
        map['f'] = '\f';
        map['n'] = '\n';
        map['r'] = '\r';
        map['t'] = '\t';
        return map;
      }();
      builder.push(MAP[ctx->buffer[ctx->i++]]);
      goto top;
    }
    case 'u': {
      ctx->i++;
      Result<Utf16Char, JsonErrorInfo> hi = json_parse_utf16(ctx);
      if (!hi) {
        return hi.error();
      }
      if (hi->value == 0 or is_low_surrogate(*hi)) {
        return JsonErrorInfo{
            .error = JsonError::InvalidCodeUnit,
            .offset = ctx->i - 6,
        };
      }
      Utf32Char cu = {};
      if (is_high_surrogate(*hi)) {
        if (ctx->i == ctx->buffer.m_size) {
          return JSON_EOF_ERROR;
        }
        if (ctx->buffer[ctx->i] != '\\') {
          return JSON_SYNTAX_ERROR;
        }
        ctx->i++;
        if (ctx->i == ctx->buffer.m_size) {
          return JSON_EOF_ERROR;
        }
        if (ctx->buffer[ctx->i] != 'u') {
          return JSON_SYNTAX_ERROR;
        }
        ctx->i++;
        Result<Utf16Char, JsonErrorInfo> lo = json_parse_utf16(ctx);
        if (!lo) {
          return lo.error();
        }
        if (lo->value == 0 or not is_low_surrogate(*lo)) {
          return JsonErrorInfo{
              .error = JsonError::InvalidCodeUnit,
              .offset = ctx->i - 6,
          };
        }
        cu = to_utf32(*hi, *lo);
      } else {
        cu = to_utf32(*hi);
      }
      to_utf8(cu, &builder);
      goto top;
    }
    default:
      return JSON_SYNTAX_ERROR;
    }
  }
  default:
    builder.push(ctx->buffer[ctx->i++]);
    goto top;
  }
}

Result<JsonValue, JsonErrorInfo>
json_parse_number(NotNull<JsonParserContext *> ctx) {
  // FIXME: EOF != invalid syntax.
  // FIXME: overflow checks.
  // FIXME: accurate double parsing.

  bool is_negative = false;
  i64 integral = 0;

  if (ctx->i == ctx->buffer.m_size) {
    return JSON_EOF_ERROR;
  }
  if (ctx->buffer[ctx->i] == '-') {
    is_negative = true;
    ctx->i++;
  }

  // FIXME: disallow numbers that start with 0.

parse_integral:
  if (ctx->i == ctx->buffer.m_size) {
    return JSON_EOF_ERROR;
  }
  switch (ctx->buffer[ctx->i]) {
  default:
    return JsonValue{
        .type = JsonType::Integer,
        .integer = is_negative ? (i64)-integral : (i64)integral,
    };
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9': {
    i64 digit = ctx->buffer[ctx->i++] - '0';
    integral = integral * 10 + digit;
    goto parse_integral;
  }
  case '.':
    ctx->i++;
    break;
  case 'e':
  case 'E':
    break;
  }

  i64 fractional = 0;
  i64 fractional_len = 0;

parse_fractional:
  if (ctx->i == ctx->buffer.m_size) {
    return JSON_EOF_ERROR;
  }
  switch (ctx->buffer[ctx->i]) {
  default: {
    if (fractional_len == 0) {
      return JSON_SYNTAX_ERROR;
    }
    double value = integral + fractional * std::pow(10.0, -fractional_len);
    return JsonValue{
        .type = JsonType::Number,
        .number = is_negative ? -value : value,
    };
  }
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9': {
    i64 digit = ctx->buffer[ctx->i++] - '0';
    fractional = fractional * 10 + digit;
    fractional_len++;
    goto parse_fractional;
  }
  case 'e':
  case 'E':
    ctx->i++;
    break;
  }

  bool is_exponent_negative = false;
  i64 exponent = 0;

  if (ctx->i == ctx->buffer.m_size) {
    return JSON_EOF_ERROR;
  }
  if (ctx->buffer[ctx->i] == '-') {
    is_exponent_negative = true;
    ctx->i++;
  } else if (ctx->buffer[ctx->i] == '+') {
    ctx->i++;
  }

parse_exponent:
  if (ctx->i == ctx->buffer.m_size) {
    return JSON_EOF_ERROR;
  }
  switch (ctx->buffer[ctx->i]) {
  default: {
    if (fractional_len == 0) {
      return JSON_SYNTAX_ERROR;
    }
    double value = integral + fractional * std::pow(10.0, -fractional_len);
    value = value * std::pow(10.0, is_exponent_negative ? -exponent : exponent);
    return JsonValue{
        .type = JsonType::Number,
        .number = is_negative ? -value : value,
    };
  }
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9': {
    i64 digit = ctx->buffer[ctx->i++] - '0';
    exponent = exponent * 10 + digit;
    goto parse_exponent;
  }
  }
}

Result<JsonValue, JsonErrorInfo> json_parse(NotNull<JsonParserContext *> ctx) {
  ScratchArena scratch;
  JsonValue json = {};
parse:
  if (ctx->i == ctx->buffer.m_size) {
    return JSON_EOF_ERROR;
  }
  switch (ctx->buffer[ctx->i]) {
  CASE_JSON_WHITESPACE:
    ctx->i++;
    goto parse;
  case '{': {
    ctx->i++;
    DynamicArray<JsonKeyValue> object;
    json.type = JsonType::Object;
  parse_object:
    if (ctx->i == ctx->buffer.m_size) {
      return JSON_EOF_ERROR;
    }
    switch (ctx->buffer[ctx->i]) {
    CASE_JSON_WHITESPACE:
      ctx->i++;
      goto parse_object;
    case '}': {
      ctx->i++;
      auto object_copy =
          Span<JsonKeyValue>::allocate(ctx->arena, object.m_size);
      copy(Span(object), object_copy.m_data);
      json.object = object_copy;
      return json;
    }
    case ',':
      if (object.m_size == 0) {
        return JSON_SYNTAX_ERROR;
      }
      ctx->i++;
      break;
    }

  parse_object_key:
    if (ctx->i == ctx->buffer.m_size) {
      return JSON_EOF_ERROR;
    }
    switch (ctx->buffer[ctx->i]) {
    CASE_JSON_WHITESPACE:
      ctx->i++;
      goto parse_object_key;
    case '"':
      ctx->i++;
      break;
    default:
      return JSON_SYNTAX_ERROR;
    }
    Result<String8, JsonErrorInfo> key = json_parse_string(ctx);
    if (!key) {
      return key.error();
    }

  parse_object_value:
    if (ctx->i == ctx->buffer.m_size) {
      return JSON_EOF_ERROR;
    }
    switch (ctx->buffer[ctx->i]) {
    CASE_JSON_WHITESPACE:
      ctx->i++;
      goto parse_object_value;
    case ':':
      ctx->i++;
      break;
    default:
      return JSON_SYNTAX_ERROR;
    }
    Result<JsonValue, JsonErrorInfo> value = json_parse(ctx);
    if (!value) {
      return value.error();
    }

    object.push(scratch, {*key, *value});

    goto parse_object;
  }
  case '[': {
    ctx->i++;
    DynamicArray<JsonValue> array;
    json.type = JsonType::Array;
  parse_array:
    if (ctx->i == ctx->buffer.m_size) {
      return JSON_EOF_ERROR;
    }
    switch (ctx->buffer[ctx->i]) {
    CASE_JSON_WHITESPACE:
      ctx->i++;
      goto parse_array;
    case ']': {
      ctx->i++;
      auto array_copy = Span<JsonValue>::allocate(ctx->arena, array.m_size);
      copy(Span(array), array_copy.m_data);
      json.array = array_copy;
      return json;
    }
    case ',':
      if (array.m_size == 0) {
        return JSON_SYNTAX_ERROR;
      }
      ctx->i++;
      break;
    }
    Result<JsonValue, JsonErrorInfo> element = json_parse(ctx);
    if (!element) {
      return element.error();
    }
    array.push(scratch, *element);
    goto parse_array;
  }
  case '"': {
    ctx->i++;
    Result<String8, JsonErrorInfo> string = json_parse_string(ctx);
    if (!string) {
      return string.error();
    }

    if (*string == "null") {
      json.type = JsonType::Null;
    } else if (*string == "true") {
      json.type = JsonType::Boolean;
      json.boolean = true;
    } else if (*string == "false") {
      json.type = JsonType::Boolean;
      json.boolean = false;
    } else {
      json.type = JsonType::String;
      json.string = *string;
    }

    return json;
  }
  case '-':
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    return json_parse_number(ctx);
  }
  return JSON_SYNTAX_ERROR;
}

Result<JsonValue, JsonErrorInfo> json_parse(NotNull<Arena *> arena,
                                            String8 buffer) {
  JsonParserContext ctx = {
      .arena = arena,
      .buffer = buffer,
  };
  Result<JsonValue, JsonErrorInfo> result = json_parse(&ctx);
  if (result) {
    return *result;
  }
  JsonErrorInfo error = result.error();
  for (usize i : range(error.offset)) {
    char c = buffer[i];
    if (c == '\n') {
      error.line++;
      error.column = 0;
    } else if (c == '\r') {
      error.column = 0;
    }
    error.column++;
  }
  return error;
}

const usize JSON_TAB_WIDTH = 2;

void json_serialize(NotNull<StringBuilder *> builder, JsonValue json,
                    usize indent) {
  switch (json.type) {
  case JsonType::Null: {
    builder->push("\"null\"");
    return;
  }
  case JsonType::Object: {
    if (json.object.m_size == 0) {
      builder->push("{}");
      return;
    }
    builder->push("{\n");
    for (usize i : range(json.object.m_size)) {
      JsonKeyValue kv = json.object[i];
      for (usize _ : range(indent + JSON_TAB_WIDTH)) {
        builder->push(" ");
      }
      builder->push('"');
      builder->push(kv.key);
      builder->push("\": ");
      json_serialize(builder, kv.value, indent + JSON_TAB_WIDTH);
      if (i < json.object.m_size - 1) {
        builder->push(',');
      }
      builder->push('\n');
    }
    for (usize _ : range(indent)) {
      builder->push(" ");
    }
    builder->push('}');
    return;
  }
  case JsonType::Array: {
    if (json.array.m_size == 0) {
      builder->push("[]");
      return;
    }

    bool any_object_or_array = false;
    for (JsonValue element : json.array) {
      if (element.type == JsonType::Object or element.type == JsonType::Array) {
        any_object_or_array = true;
        break;
      }
    }

    if (any_object_or_array or json.array.m_size > 4) {
      builder->push("[\n");
      for (usize i : range(json.array.m_size)) {
        JsonValue element = json.array[i];
        for (usize _ : range(indent + JSON_TAB_WIDTH)) {
          builder->push(" ");
        }
        json_serialize(builder, element, indent + JSON_TAB_WIDTH);
        if (i < json.object.m_size - 1) {
          builder->push(',');
        }
        builder->push('\n');
      }
      for (usize _ : range(indent)) {
        builder->push(" ");
      }
      builder->push(']');

    } else {
      builder->push("[ ");
      for (usize i : range(json.array.m_size)) {
        json_serialize(builder, json.array[i], 0);
        if (i < json.object.m_size - 1) {
          builder->push(',');
        }
        builder->push(' ');
      }
      builder->push(']');
    }
    return;
  }
  case JsonType::String: {
    builder->push('"');
    builder->push(json.string);
    builder->push('"');
    return;
  }
  case JsonType::Integer: {
    format_to(builder, "{}", json.integer);
    return;
  }
  case JsonType::Number: {
    format_to(builder, "{}", json.number);
    return;
  }
  case JsonType::Boolean: {
    if (json.boolean) {
      builder->push("\"true\"");
    } else {
      builder->push("\"false\"");
    }
    return;
  }
  }
}

String8 json_serialize(NotNull<Arena *> arena, JsonValue json) {
  ScratchArena scratch;
  auto builder = StringBuilder::init(scratch);
  json_serialize(&builder, json, 0);
  return builder.materialize(arena);
}

} // namespace ren
