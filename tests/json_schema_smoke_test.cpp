// json_schema_smoke_test.cpp — Stage 20 pinning.
//
// Runs `av::json_schema<T>()` at consteval and pins the exact byte
// output via `static_assert`. If an annotation's schema contribution
// drifts, or the emitter changes ordering, these fail at compile
// time with the diff visible in the diagnostic.
//
// Coverage (one static_assert per shape):
//   - Flat struct: integer+Range, string+MinLength+MaxLength
//   - Nested aggregate (Address inside User)
//   - std::optional<T> + NotNullopt: unwraps to T, lifts into required
//   - std::vector<T> + MinSize/MaxSize: "items" plus array size bounds
//   - Floating-point + Range -> "type":"number"
//   - Predicate -> "$comment" marker (not enforceable, surfaced honestly)
//   - NotEmpty on string / vector -> minLength:1 / minItems:1
//   - All-required object: every member carries NotNullopt
//   - None-required object: "required" key omitted entirely

#include <validator.hpp>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace av;

// (1) Flat.
struct Flat {
    [[=Range{0, 150}]]                int         age;
    [[=MinLength{3}, =MaxLength{64}]] std::string name;
};

static_assert(json_schema<Flat>() == std::string{
    R"({"type":"object","properties":{)"
    R"("age":{"type":"integer","minimum":0,"maximum":150},)"
    R"("name":{"type":"string","minLength":3,"maxLength":64})"
    R"(}})"
});

// (2) Nested aggregate + optional + NotNullopt + vector.
struct Address {
    [[=MinLength{2}]]    std::string street;
    [[=Range{1, 99999}]] int         zip_code;
};

struct User {
    [[=Range{0, 150}]]                int                        age;
    [[=MinLength{3}, =MaxLength{64}]] std::string                name;
    [[=NotNullopt{}]]                 std::optional<std::string> email;
    Address                                                      address;
    [[=MinSize{1}, =MaxSize{10}]]     std::vector<std::string>   tags;
};

static_assert(json_schema<User>() == std::string{
    R"({"type":"object","properties":{)"
    R"("age":{"type":"integer","minimum":0,"maximum":150},)"
    R"("name":{"type":"string","minLength":3,"maxLength":64},)"
    R"("email":{"type":"string"},)"
    R"("address":{"type":"object","properties":{)"
    R"("street":{"type":"string","minLength":2},)"
    R"("zip_code":{"type":"integer","minimum":1,"maximum":99999})"
    R"(}},)"
    R"("tags":{"type":"array","minItems":1,"maxItems":10,"items":{"type":"string"}})"
    R"(},"required":["email"]})"
});

// (3) Floating-point + Range, Predicate -> "$comment".
constexpr bool is_not_nan(double x) { return x == x; }

struct Measurement {
    [[=Range{-40, 120}]]                                double  temperature_c;
    [[=Predicate{is_not_nan, "value must not be NaN"}]] double  reading;
};

static_assert(json_schema<Measurement>() == std::string{
    R"({"type":"object","properties":{)"
    R"("temperature_c":{"type":"number","minimum":-40,"maximum":120},)"
    R"("reading":{"type":"number","$comment":"predicate: value must not be NaN"})"
    R"(}})"
});

// (4) NotEmpty.
struct Bag {
    [[=NotEmpty{}]] std::string              label;
    [[=NotEmpty{}]] std::vector<int>         items;
};

static_assert(json_schema<Bag>() == std::string{
    R"({"type":"object","properties":{)"
    R"("label":{"type":"string","minLength":1},)"
    R"("items":{"type":"array","minItems":1,"items":{"type":"integer"}})"
    R"(}})"
});

// (5) All members required.
struct AllReq {
    [[=NotNullopt{}]] std::optional<int>         id;
    [[=NotNullopt{}]] std::optional<std::string> token;
};

static_assert(json_schema<AllReq>() == std::string{
    R"({"type":"object","properties":{)"
    R"("id":{"type":"integer"},)"
    R"("token":{"type":"string"})"
    R"(},"required":["id","token"]})"
});

// (6) No required — "required" key must be absent.
struct NoReq {
    int         a;
    std::string b;
};

static_assert(json_schema<NoReq>() == std::string{
    R"({"type":"object","properties":{)"
    R"("a":{"type":"integer"},)"
    R"("b":{"type":"string"})"
    R"(}})"
});

int main() {
    std::cout << "json schema smoke test: OK\n";

    // Runtime cross-check: the consteval-produced string equals the
    // runtime-produced string, byte for byte. Same function, same
    // result across the two evaluation phases.
    const auto rt = json_schema<User>();
    const auto ct = std::string{
        R"({"type":"object","properties":{)"
        R"("age":{"type":"integer","minimum":0,"maximum":150},)"
        R"("name":{"type":"string","minLength":3,"maxLength":64},)"
        R"("email":{"type":"string"},)"
        R"("address":{"type":"object","properties":{)"
        R"("street":{"type":"string","minLength":2},)"
        R"("zip_code":{"type":"integer","minimum":1,"maximum":99999})"
        R"(}},)"
        R"("tags":{"type":"array","minItems":1,"maxItems":10,"items":{"type":"string"}})"
        R"(},"required":["email"]})"
    };
    if (rt != ct) {
        std::cerr << "runtime/consteval mismatch\n"
                  << "runtime:   " << rt << '\n'
                  << "consteval: " << ct << '\n';
        return 1;
    }

    return 0;
}
