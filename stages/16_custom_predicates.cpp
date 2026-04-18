// Stage 16: Custom predicate annotations via Predicate<F> wrapper.
//
// Through Stage 15 the annotation set is closed. Range, MinLength,
// MaxLength, MinSize, MaxSize, NotNullopt — the dispatch switch at the
// top of validate_impl enumerates exactly those, and a user who wants
// "name must start with an uppercase letter" or "age is even" has no way
// in. This stage opens the last door: carrying a user-provided callable
// through the [[=...]] syntax.
//
// The interesting question isn't "can we run a user-provided function" —
// that's trivial in any language. It's whether the annotation / NTTP /
// structural-type rules let us carry a callable *inside* an annotation.
// Three sub-questions:
//
//   Q1 (baseline): does a captureless lambda closure work as an NTTP?
//     The C++26 draft makes captureless closure types structural, which
//     is the prerequisite for NTTP use. If clang-p2996 implements the
//     rule, [[=Predicate{[](const T&){ return ...; }}]] compiles and
//     the field gets validated.
//
//   Q2 (expected failure): capturing lambda. Closure types with captures
//     are not structural, so the annotation declaration itself should be
//     rejected. Goal is to confirm the error and document its wording —
//     this is intentionally out of scope for the stage file; the blog
//     records the minimal reproducer and the error verbatim.
//
//   Q3 (site-identity probe, cf. Stage 12's regex fold): two fields with
//     textually identical lambda expressions. Stage 12 showed that
//     same-pattern Regex annotations fold to one template instantiation
//     because Regex<N> is keyed by the char array value. Lambdas are
//     different: every lambda-expression creates a *distinct* closure
//     type, regardless of spelling. So two fields with the "same"
//     captureless lambda should end up with distinct Predicate<F>
//     instantiations. This isn't a bug — it's the language — but it
//     means "same validator textually" does not imply "same compiled
//     code," and any user-facing dedup would need to key on something
//     richer than the annotation type.
//
// Design choices, staked up front:
//   - Wrapper annotation (Predicate<F>), not a raw callable. Gives
//     dispatch a named handle, leaves room for later metadata (name,
//     message, code), matches the shape of Range / MinLength.
//   - Call contract: bool f(const T&). Fixed error message
//     "custom predicate failed" for this stage. Richer diagnostics are
//     deferred — callable-shape experiments and error-message design
//     should not land together or the failure mode gets muddled.
//   - Silent skip on type mismatch, matching the Range / MinLength
//     dispatch. Annotating a string with Predicate<bool(*)(int)> is
//     ignored the same way annotating a string with Range is ignored.

#include <meta>
#include <iostream>
#include <string>
#include <vector>
#include <format>
#include <cstddef>
#include <optional>
#include <variant>
#include <type_traits>
#include <concepts>
#include <typeinfo>

// ---- Annotation types (subset carried from previous stages) ----

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}
};

struct MinLength {
    std::size_t value;
    constexpr MinLength(std::size_t v) : value(v) {}
};

// ---- New: Predicate<F> wrapper ----
//
// F is expected to be a structural type invocable as bool f(const T&)
// for some T matching the annotated field. The canonical F here is a
// captureless lambda's closure type (structural in C++26), but any
// structural callable — function pointer, structural functor — works
// equivalently.

template <typename F>
struct Predicate {
    F f;
};

// ---- Type traits ----

template <typename T>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};
template <typename T>
constexpr bool is_optional_v = is_optional<T>::value;

template <typename T>
struct is_vector : std::false_type {};
template <typename T>
struct is_vector<std::vector<T>> : std::true_type {};
template <typename T>
constexpr bool is_vector_v = is_vector<T>::value;

// ---- Error + context ----

struct ValidationError {
    std::string path;
    std::string message;
    std::string annotation;
};

using PathSegment = std::variant<std::string, std::size_t>;

struct ValidationContext {
    std::vector<ValidationError> errors;
    std::vector<PathSegment> path_stack;

    std::string current_path() const {
        std::string r;
        for (const auto& seg : path_stack) {
            if (std::holds_alternative<std::size_t>(seg)) {
                r += std::format("[{}]", std::get<std::size_t>(seg));
            } else {
                const auto& name = std::get<std::string>(seg);
                if (!r.empty()) r += '.';
                r += name;
            }
        }
        return r;
    }
};

// ---- Validator ----

template <typename T>
void validate_impl(const T& obj, ValidationContext& ctx) {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked())))
    {
        ctx.path_stack.push_back(
            std::string{std::meta::identifier_of(member)});

        using MT = std::remove_cvref_t<decltype(obj.[:member:])>;

        template for (constexpr auto ann :
                      std::define_static_array(
                          std::meta::annotations_of(member)))
        {
            if constexpr (std::meta::type_of(ann) == ^^Range) {
                if constexpr (requires { obj.[:member:] < 0LL; }
                           && !is_optional_v<MT>
                           && !is_vector_v<MT>) {
                    constexpr auto r = std::meta::extract<Range>(ann);
                    auto v = obj.[:member:];
                    if (v < r.min || v > r.max) {
                        ctx.errors.push_back({
                            ctx.current_path(),
                            std::format("must be in [{}, {}], got {}",
                                        r.min, r.max, v),
                            "Range"
                        });
                    }
                }
            } else if constexpr (std::meta::type_of(ann) == ^^MinLength) {
                if constexpr (requires { obj.[:member:].size(); }
                           && !is_optional_v<MT>
                           && !is_vector_v<MT>) {
                    constexpr auto r = std::meta::extract<MinLength>(ann);
                    const auto& v = obj.[:member:];
                    if (v.size() < r.value) {
                        ctx.errors.push_back({
                            ctx.current_path(),
                            std::format("length must be >= {}, got {}",
                                        r.value, v.size()),
                            "MinLength"
                        });
                    }
                }
            } else if constexpr (std::meta::template_of(
                                     std::meta::type_of(ann)) == ^^Predicate) {
                // Predicate<F> dispatch. Recovery recipe reuses Stage 12's
                // Regex<N> shape — template_arguments_of to get F, then
                // extract<Predicate<F>>(ann) for the value. The only new
                // ingredient is that targs[0] is a TYPE reflection (F is a
                // type parameter), so we splice it into a using-alias.
                // Stage 9's splice wall was about splice-as-template-arg
                // from a loop variable; splicing into a using-alias from a
                // local constexpr is a different path and is expected to
                // work.
                constexpr auto targs = std::define_static_array(
                    std::meta::template_arguments_of(
                        std::meta::type_of(ann)));
                using F = [:targs[0]:];
                if constexpr (requires(F g) {
                                  { g(obj.[:member:]) }
                                      -> std::same_as<bool>;
                              }) {
                    constexpr auto p = std::meta::extract<Predicate<F>>(ann);
                    if (!p.f(obj.[:member:])) {
                        ctx.errors.push_back({
                            ctx.current_path(),
                            "custom predicate failed",
                            "Predicate"
                        });
                    }
                }
            }
        }

        if constexpr (is_optional_v<MT>) {
            using Inner = typename MT::value_type;
            if constexpr (std::is_aggregate_v<Inner>) {
                if (obj.[:member:].has_value()) {
                    validate_impl(*obj.[:member:], ctx);
                }
            }
        } else if constexpr (is_vector_v<MT>) {
            using Elem = typename MT::value_type;
            if constexpr (std::is_aggregate_v<Elem>) {
                const auto& vec = obj.[:member:];
                for (std::size_t i = 0; i < vec.size(); ++i) {
                    ctx.path_stack.push_back(i);
                    validate_impl(vec[i], ctx);
                    ctx.path_stack.pop_back();
                }
            }
        } else if constexpr (std::is_aggregate_v<MT>) {
            validate_impl(obj.[:member:], ctx);
        }

        ctx.path_stack.pop_back();
    }
}

// ---- Demo ----

// Named function object: structural by virtue of being an empty
// aggregate. The "portable / minimal path" — any structural callable
// qualifies as F, not just closure types.
struct IsPositive {
    constexpr bool operator()(int v) const { return v > 0; }
};

struct User {
    // Q1: captureless lambda on a string.
    [[=Predicate{[](const std::string& s) {
        return !s.empty() && s[0] >= 'A' && s[0] <= 'Z';
    }}]]
    std::string name;

    // Q1: captureless lambda on an int.
    [[=Predicate{[](int v) { return v % 2 == 0; }}]]
    int even_number;

    // Named functor (Design C): structural empty aggregate with
    // constexpr operator(). Works through the same Predicate<F> path —
    // F is just IsPositive instead of a closure type.
    [[=Predicate{IsPositive{}}]]
    int positive;

    // Coexistence: Range and Predicate on the same field. Both branches
    // fire in the closed-set / Predicate order; errors accumulate.
    [[=Range{0, 150}, =Predicate{[](int v) { return v != 13; }}]]
    int age;
};

// Q3 probe. Two fields with *textually identical* captureless lambdas.
// Each lambda-expression produces its own distinct closure type, so the
// annotation types differ even though the source text matches. The probe
// below prints typeid of F for each site; we expect two distinct types.
struct TwoSites {
    [[=Predicate{[](int x) { return x > 0; }}]] int a;
    [[=Predicate{[](int x) { return x > 0; }}]] int b;
};

template <typename T>
void probe_predicate_types(const T&) {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked())))
    {
        template for (constexpr auto ann :
                      std::define_static_array(
                          std::meta::annotations_of(member)))
        {
            if constexpr (std::meta::template_of(
                              std::meta::type_of(ann)) == ^^Predicate) {
                constexpr auto targs = std::define_static_array(
                    std::meta::template_arguments_of(
                        std::meta::type_of(ann)));
                using F = [:targs[0]:];
                std::cout << "  field "
                          << std::meta::identifier_of(member)
                          << "   sizeof(F)=" << sizeof(F)
                          << "   typeid(F)=" << typeid(F).name()
                          << '\n';
            }
        }
    }
}

int main() {
    // Case 1: each predicate failing.
    User u1{
        .name = "alice",            // fails: lowercase first char
        .even_number = 7,           // fails: odd
        .positive = -3,             // fails: non-positive
        .age = 13                   // fails: predicate (Range passes)
    };
    ValidationContext c1;
    validate_impl(u1, c1);

    std::cout << "---- case 1: predicates failing ----\n";
    for (const auto& e : c1.errors) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }

    // Case 2: all valid.
    User u2{
        .name = "Alice",
        .even_number = 4,
        .positive = 10,
        .age = 30
    };
    ValidationContext c2;
    validate_impl(u2, c2);

    std::cout << "\n---- case 2: all valid ----\n";
    if (c2.errors.empty()) std::cout << "  (no errors)\n";
    for (const auto& e : c2.errors) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }

    // Case 3: Range and Predicate on the same field, both firing.
    User u3{
        .name = "Bob",
        .even_number = 2,
        .positive = 1,
        .age = 200                  // Range fails, predicate passes
    };
    ValidationContext c3;
    validate_impl(u3, c3);

    std::cout << "\n---- case 3: Range and Predicate coexist ----\n";
    for (const auto& e : c3.errors) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }

    // Q3: site-identity probe. Two fields, two textually identical
    // lambdas. Each lambda-expression gets its own closure type.
    std::cout << "\n---- Q3: site-identity probe ----\n";
    std::cout << "(two fields, textually identical captureless lambdas)\n";
    TwoSites ts{1, 2};
    probe_predicate_types(ts);

    return 0;
}
