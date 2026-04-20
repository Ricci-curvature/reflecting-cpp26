// Probe 1 for Stage 19: is std::format usable inside constant evaluation
// on clang-p2996?
//
// Stage 19 wants to run the whole walker at compile time so that
// `static_assert(av::passes(literal_struct));` rejects annotation
// violations as compile errors. Each annotation's validate() body
// currently composes its message with std::format:
//
//   ctx.errors.push_back({
//       ctx.current_path(),
//       std::format("must be in [{}, {}], got {}", min, max, v),
//       "Range"
//   });
//
// If std::format can't be called in a constexpr context on this
// compiler — or if its return value can't flow through the transient
// std::vector<ValidationError> we keep inside ValidationContext —
// Stage 19 has to either (a) swap annotations to a simpler message
// format or (b) settle for "no rich message at compile time."
//
// Four forms, from narrow to wide:
//
//   (1) std::format called inside a consteval function, the returned
//       std::string interrogated but not escaped. Baseline — is
//       std::format consteval at all on this toolchain?
//
//   (2) Inside a consteval function, a transient std::vector<std::string>
//       receiving several std::format results. Mimics the push_back
//       pattern the walker uses.
//
//   (3) Same, but with a struct carrying three std::string members
//       (shape of av::ValidationError). Closest analogue to what
//       ValidationContext::errors actually holds.
//
//   (4) A namespace-scope `constexpr std::string` holding a small
//       formatted result. This is the strict form — constexpr storage
//       requires no heap remnants past constant evaluation, so it only
//       works for strings that fit SSO. Mostly a curiosity for Stage 19
//       (the walker only needs the transient forms), but useful to
//       know the wall.

#include <format>
#include <string>
#include <vector>
#include <cstddef>

struct Error {
    std::string path;
    std::string message;
    std::string annotation;
};

// (1) std::format in a consteval function, returning a scalar derived
//     from the formatted std::string. Does format even participate in
//     constant expressions?
consteval std::size_t form_1() {
    auto s = std::format("got {}", 42);
    return s.size();  // "got 42" -> 6
}

// (2) Transient std::vector<std::string> with several formatted entries.
//     Stresses vector<string>::push_back at constexpr.
consteval std::size_t form_2() {
    std::vector<std::string> v;
    v.push_back(std::format("first {}", 1));
    v.push_back(std::format("second {}", 2));
    v.push_back(std::format("third {}", 3));
    return v.size();
}

// (3) Transient std::vector<Error> — three-string struct, one of the
//     strings is a std::format result. Same shape as ValidationContext.
consteval std::size_t form_3() {
    std::vector<Error> errs;
    errs.push_back({
        std::string{"user.age"},
        std::format("must be in [{}, {}], got {}", 0LL, 150LL, 200LL),
        std::string{"Range"}
    });
    errs.push_back({
        std::string{"user.name"},
        std::format("length must be >= {}, got {}",
                    std::size_t{3}, std::size_t{2}),
        std::string{"MinLength"}
    });
    return errs.size();
}

// (4) Namespace-scope constexpr std::string. Strict form — may fail
//     with non-SSO strings. Start with something small.
constexpr std::string small_fmt = std::format("x={}", 42);

// Verification.
static_assert(form_1() == 6);
static_assert(form_2() == 3);
static_assert(form_3() == 2);
static_assert(small_fmt.size() == 4);  // "x=42"

int main() {}
