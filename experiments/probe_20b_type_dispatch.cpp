// Probe 2 for Stage 20: type dispatch for the "type":… JSON Schema
// keyword.
//
// Stage 20's core emitter is, roughly:
//
//   for each member m of T:
//     if constexpr (is_integral_v<V>)       frag = emit_integer(...)
//     else if constexpr (is_floating_point_v<V>) frag = emit_number(...)
//     else if constexpr (is_string_v<V>)    frag = emit_string(...)
//     else if constexpr (is_vector_v<V>)    frag = emit_array(...)
//     else                                  frag = "{}"   // fallback
//
// with each branch layering per-annotation constraints on top (e.g.
// Range → minimum/maximum, MinLength → minLength).
//
// The open question from the Stage 20 plan is floating-point: does
// `double` + `Range{0,150}` compose honestly into
// `{"type":"number","minimum":0,"maximum":150}`, or does it fall over
// somewhere (Range stores long long — does the comparison path at
// consteval behave)? This probe exercises that directly.
//
// We don't need reflection here. A free-function `schema_of<V>(ann)`
// templated on the value type covers the dispatch we care about.

#include <charconv>
#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

// ---- Mini-formatter ----

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

// ---- Trait helpers ----

template <typename T>
struct is_std_string : std::false_type {};
template <>
struct is_std_string<std::string> : std::true_type {};

template <typename T>
struct is_std_vector : std::false_type {};
template <typename U>
struct is_std_vector<std::vector<U>> : std::true_type {};

// ---- Annotations (schema-only shape for this probe) ----

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}
};

struct MinLength {
    std::size_t value;
    constexpr MinLength(std::size_t v) : value(v) {}
};

// ---- Per-type emit with per-annotation overlay ----

// Overload: Range on integral field → minimum/maximum in an "integer" schema.
template <typename V>
    requires std::is_integral_v<V>
constexpr std::string emit(Range r) {
    std::string s;
    s += R"({"type":"integer","minimum":)";
    s += int_to_str(r.min);
    s += R"(,"maximum":)";
    s += int_to_str(r.max);
    s += '}';
    return s;
}

// Overload: Range on floating-point field → minimum/maximum in a "number" schema.
template <typename V>
    requires std::is_floating_point_v<V>
constexpr std::string emit(Range r) {
    std::string s;
    s += R"({"type":"number","minimum":)";
    s += int_to_str(r.min);
    s += R"(,"maximum":)";
    s += int_to_str(r.max);
    s += '}';
    return s;
}

// Overload: MinLength on std::string field → "string" with minLength.
template <typename V>
    requires is_std_string<V>::value
constexpr std::string emit(MinLength m) {
    std::string s;
    s += R"({"type":"string","minLength":)";
    s += uint_to_str(m.value);
    s += '}';
    return s;
}

// Bare-type helper (no annotation): used when we only know the field type
// and want the "type" keyword alone.
template <typename V>
constexpr std::string emit_bare_type() {
    if constexpr (std::is_integral_v<V>) {
        return std::string{R"({"type":"integer"})"};
    } else if constexpr (std::is_floating_point_v<V>) {
        return std::string{R"({"type":"number"})"};
    } else if constexpr (is_std_string<V>::value) {
        return std::string{R"({"type":"string"})"};
    } else if constexpr (is_std_vector<V>::value) {
        return std::string{R"({"type":"array"})"};
    } else {
        return std::string{"{}"};
    }
}

// ---- Checks ----

// (1) Integral + Range.
static_assert(emit<int>(Range{0, 150}) ==
              std::string{R"({"type":"integer","minimum":0,"maximum":150})"});

// (2) Floating-point + Range — the decision point.
static_assert(emit<double>(Range{0, 150}) ==
              std::string{R"({"type":"number","minimum":0,"maximum":150})"});

// (3) std::string + MinLength.
static_assert(emit<std::string>(MinLength{3}) ==
              std::string{R"({"type":"string","minLength":3})"});

// (4) Bare type dispatch for all four families.
static_assert(emit_bare_type<int>()           == std::string{R"({"type":"integer"})"});
static_assert(emit_bare_type<long long>()     == std::string{R"({"type":"integer"})"});
static_assert(emit_bare_type<double>()        == std::string{R"({"type":"number"})"});
static_assert(emit_bare_type<float>()         == std::string{R"({"type":"number"})"});
static_assert(emit_bare_type<std::string>()   == std::string{R"({"type":"string"})"});
static_assert(emit_bare_type<std::vector<int>>() == std::string{R"({"type":"array"})"});

int main() {}
