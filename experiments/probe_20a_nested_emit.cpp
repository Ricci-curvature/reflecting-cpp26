// Probe 1 for Stage 20: can we compose a nested JSON Schema string
// at consteval using the Stage 19 mini-formatter (std::to_chars +
// std::string concat)?
//
// Stage 19 established that std::to_chars is constexpr under this
// libc++ and that std::format is not. The walker + annotation
// validate() bodies already run at consteval. The remaining question
// for Stage 20 is mechanical: once we have per-field fragments like
//
//   {"type":"integer","minimum":0,"maximum":150}
//
// can we concat them into an outer
//
//   {"type":"object","properties":{"age":<frag>,"name":<frag>},
//    "required":["age","name"]}
//
// at consteval, or does something in std::string's growth / concat
// path trip up consteval in a way we haven't seen?
//
// No reflection here — pure string composition. If this compiles and
// the static_assert passes, the mechanics are cleared and Stage 20
// can focus on the reflection binding in probe 20b / 20c.

#include <charconv>
#include <cstddef>
#include <string>

// ---- Mini-formatter (carry-over from Stage 19) ----

constexpr std::string int_to_str(long long x) {
    char buf[24];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), x);
    return std::string(buf, ptr);
}

constexpr std::string uint_to_str(std::size_t x) {
    char buf[24];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), x);
    return std::string(buf, ptr);
}

// ---- Per-type fragment emitters ----

constexpr std::string emit_integer_schema(long long min, long long max) {
    std::string r;
    r += R"({"type":"integer","minimum":)";
    r += int_to_str(min);
    r += R"(,"maximum":)";
    r += int_to_str(max);
    r += '}';
    return r;
}

constexpr std::string emit_string_schema(std::size_t min_len) {
    std::string r;
    r += R"({"type":"string","minLength":)";
    r += uint_to_str(min_len);
    r += '}';
    return r;
}

// ---- Outer composition ----
//
// Shape:
//   {"type":"object",
//    "properties":{ "age":<frag>,"name":<frag> },
//    "required":["age","name"]}

constexpr std::string emit_quoted(std::string_view s) {
    std::string r;
    r += '"';
    r += s;
    r += '"';
    return r;
}

constexpr std::string emit_user_schema() {
    const std::string age_frag  = emit_integer_schema(0, 150);
    const std::string name_frag = emit_string_schema(3);

    std::string r;
    r += R"({"type":"object","properties":{)";
    r += emit_quoted("age");
    r += ':';
    r += age_frag;
    r += ',';
    r += emit_quoted("name");
    r += ':';
    r += name_frag;
    r += R"(},"required":[)";
    r += emit_quoted("age");
    r += ',';
    r += emit_quoted("name");
    r += "]}";
    return r;
}

// ---- Checks ----

static_assert(emit_integer_schema(0, 150) ==
              std::string{R"({"type":"integer","minimum":0,"maximum":150})"});

static_assert(emit_string_schema(3) ==
              std::string{R"({"type":"string","minLength":3})"});

static_assert(emit_user_schema() == std::string{
    R"({"type":"object","properties":{"age":{"type":"integer","minimum":0,"maximum":150},"name":{"type":"string","minLength":3}},"required":["age","name"]})"
});

int main() {}
