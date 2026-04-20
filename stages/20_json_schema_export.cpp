// Stage 20: JSON Schema export — same walker, same annotations,
// different projection.
//
// Stage 18 made every annotation own its `validate(v, ctx)`.
// Stage 19 lifted that body into constexpr.
// Stage 20 adds a parallel projection on each annotation:
//
//     template <typename V>
//     constexpr void schema_emit(SchemaContext&) const;
//
// The walker asks `requires { a.template schema_emit<V>(sc); }` and
// calls through, exactly mirroring the validate() protocol. If an
// annotation can contribute to the schema, it does; if not (e.g.
// a user's annotation with no schema meaning), it's silently skipped.
//
// Per-annotation contributions:
//   Range       → "minimum":…, "maximum":…     (arithmetic, not bool)
//   MinLength   → "minLength":…                (string)
//   MaxLength   → "maxLength":…                (string)
//   NotEmpty    → "minLength":1 / "minItems":1 (string / vector)
//   MinSize     → "minItems":…                 (vector)
//   MaxSize     → "maxItems":…                 (vector)
//   NotNullopt  → (structural) flips parent's required flag
//   Predicate   → "$comment":"predicate: …"   (honest — not enforceable
//                                              in JSON Schema, but
//                                              surfaced as a marker)
//
// Three probes established the mechanics before the header changes:
//   experiments/probe_20a_nested_emit.cpp        — consteval string concat + to_chars composing a nested object frag
//   experiments/probe_20b_type_dispatch.cpp      — `is_integral` / `is_floating_point` / `is_std_string` / `is_vector` dispatch;
//                                                   `double` + `Range` emits `"type":"number","minimum":…,"maximum":…`
//   experiments/probe_20c_notnullopt_structural.cpp — two-bucket pass (properties + required), empty required omitted
//
// The public entry is `av::json_schema<T>()` — a `constexpr std::string`
// callable at runtime or via `static_assert(av::json_schema<T>() == R"(…)")`.

#include <validator.hpp>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace av;

// ---- Demo types ----

struct Address {
    [[=MinLength{2}]]     std::string street;
    [[=Range{1, 99999}]]  int         zip_code;
};

struct User {
    [[=Range{0, 150}]]                  int                        age;
    [[=MinLength{3}, =MaxLength{64}]]   std::string                name;
    [[=NotNullopt{}]]                   std::optional<std::string> email;
    Address                                                         address;
    [[=MinSize{1}, =MaxSize{10}]]       std::vector<std::string>    tags;
};

// A struct that exercises floating-point + Range and Predicate + $comment.
constexpr bool is_not_nan(double x) { return x == x; }

struct Measurement {
    [[=Range{-40, 120}]]                               double      temperature_c;
    [[=Predicate{is_not_nan, "value must not be NaN"}]] double     reading;
};

int main() {
    // (1) Full User schema — walks through every annotation type we
    //     teach in the header, plus nested Address, plus vector<string>
    //     with size bounds, plus optional<string> with NotNullopt.
    std::cout << "=== User schema ===\n";
    std::cout << json_schema<User>() << '\n';

    // (2) Floating-point + custom predicate.
    std::cout << "\n=== Measurement schema ===\n";
    std::cout << json_schema<Measurement>() << '\n';

    // (3) Same function works at compile time — see tests/json_schema_smoke_test.cpp
    //     for the static_assert pinning.
    constexpr bool nonempty = !json_schema<User>().empty();
    static_assert(nonempty);

    return 0;
}
