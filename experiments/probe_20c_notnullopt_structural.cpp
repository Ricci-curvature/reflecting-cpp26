// Probe 3 for Stage 20: NotNullopt is structurally different from the
// other annotations.
//
// Range / MinLength / Regex are *field-local*: their schema
// contribution lives inside the member's own fragment
// (`"minimum":0`, `"maxLength":64`, etc.).
//
// NotNullopt is *parent-level*: JSON Schema expresses "this field
// must be present" as an entry in the enclosing object's "required"
// array, not inside the member's own schema. A nullable type like
// `std::optional<std::string>` without NotNullopt emits a normal
// string fragment at the member, and stays out of "required".
// With NotNullopt, the fragment is unchanged, but the member's name
// is appended to the parent's "required" list.
//
// Concretely we want the emitter to run a two-bucket pass per member:
//
//   bucket A: properties — always contribute a fragment.
//   bucket B: required   — contribute the member name iff NotNullopt
//                          appears in the member's annotation pack.
//
// This probe tests the mechanics without reflection: we fake a member
// description as (name, field-type-tag, has_not_nullopt) and run the
// outer composition by hand. If the "required" array composes in the
// right order and the `"required"` key is omitted when the list is
// empty, the cross-cutting mechanism is green and Stage 20 just needs
// to wire this to `annotations_of(Member)`.

#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
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

constexpr std::string emit_quoted(std::string_view s) {
    std::string r;
    r += '"';
    r += s;
    r += '"';
    return r;
}

// ---- Minimal fragment (stand-in for Stage 20's per-member emit) ----

struct MemberDesc {
    std::string name;
    std::string fragment;   // e.g. {"type":"integer","minimum":0,...}
    bool        required;   // NotNullopt present on this member
};

constexpr std::string emit_object(const std::vector<MemberDesc>& members) {
    std::string r;
    r += R"({"type":"object","properties":{)";
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (i) r += ',';
        r += emit_quoted(members[i].name);
        r += ':';
        r += members[i].fragment;
    }
    r += '}';

    // Cross-cutting pass: collect names of required members.
    std::vector<std::string> required_names;
    for (const auto& m : members) {
        if (m.required) required_names.push_back(m.name);
    }

    if (!required_names.empty()) {
        r += R"(,"required":[)";
        for (std::size_t i = 0; i < required_names.size(); ++i) {
            if (i) r += ',';
            r += emit_quoted(required_names[i]);
        }
        r += ']';
    }

    r += '}';
    return r;
}

// ---- Scenarios ----

// (1) Mixed: `int age` required, `optional<string> nickname` not required.
consteval std::string scenario_mixed() {
    std::vector<MemberDesc> ms;
    ms.push_back({
        "age",
        std::string{R"({"type":"integer","minimum":0,"maximum":150})"},
        /*required=*/true
    });
    ms.push_back({
        "nickname",
        std::string{R"({"type":"string"})"},
        /*required=*/false
    });
    return emit_object(ms);
}

// (2) All required: two NotNullopt fields.
consteval std::string scenario_all_required() {
    std::vector<MemberDesc> ms;
    ms.push_back({"id",    std::string{R"({"type":"integer"})"}, true});
    ms.push_back({"email", std::string{R"({"type":"string"})"},  true});
    return emit_object(ms);
}

// (3) None required: "required" key must be omitted entirely.
consteval std::string scenario_none_required() {
    std::vector<MemberDesc> ms;
    ms.push_back({"a", std::string{R"({"type":"integer"})"}, false});
    ms.push_back({"b", std::string{R"({"type":"string"})"},  false});
    return emit_object(ms);
}

// ---- Checks ----

static_assert(scenario_mixed() == std::string{
    R"({"type":"object","properties":{"age":{"type":"integer","minimum":0,"maximum":150},"nickname":{"type":"string"}},"required":["age"]})"
});

static_assert(scenario_all_required() == std::string{
    R"({"type":"object","properties":{"id":{"type":"integer"},"email":{"type":"string"}},"required":["id","email"]})"
});

static_assert(scenario_none_required() == std::string{
    R"({"type":"object","properties":{"a":{"type":"integer"},"b":{"type":"string"}}})"
});

int main() {}
