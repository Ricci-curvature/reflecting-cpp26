// Stage 17: Dispatch refactor — one annotation ladder, three payoffs.
//
// Through Stage 16 the walker treats each aggregate member as one "site"
// where the annotation ladder fires. That closes three real cases:
//
//   - optional<Scalar> with a scalar annotation: Stage 13 deferred
//     [[=Range{0,150}]] optional<int>. The old ladder saw `optional` and
//     the scalar-shaped guard silently skipped it, so the constraint
//     never reached the wrapped int.
//   - vector<Scalar> with a scalar annotation: Stage 14 deferred
//     [[=Range{0,150}]] vector<int>. Same reason: the vector was neither
//     integral nor sized-like-a-string at the right moment.
//   - vector<optional<Aggregate>>: Stage 16's closing note — two layers
//     of wrapping, each needing its own recursion rule.
//
// This stage changes exactly one thing: the annotation ladder runs
// against the *value currently in hand*, not against "the member of the
// parent struct". Wrapper recursion then peels optional / vector /
// aggregate and re-enters the same ladder on the inner value. The
// ladder's guards already encode "does this annotation apply to this
// value?" via their requires-clauses; they were just being called with
// the wrong V.
//
// Three sub-questions, mapped to the refactor:
//
//   Q1 (single-wrapper inheritance): the scalar ladder fires on
//     optional<int> → int and vector<int> → int through the refactor. No
//     new annotation types, no syntax change — the annotation traverses
//     one wrapper.
//
//   Q2 (nested composition): vector<optional<Aggregate>> recurses
//     correctly, producing paths like past_addresses[0].street. Two
//     wrappers and an aggregate-walk all compose because the same
//     dispatch handles each level.
//
//   Q3 (scope selection by callable signature): two Predicate
//     annotations on the same vector<int> field — one taking
//     `const vector<int>&`, one taking `int`. The per-annotation
//     requires-guard picks which level the predicate fires at. This is
//     the proof that annotations traverse wrappers: same syntax, same
//     field, signature decides the apply-site.
//
// Architecture:
//   - walk_members<T>(obj, ctx) iterates aggregate members.
//   - dispatch_value<Member, V>(v, ctx) is the reusable site — it runs
//     the annotation ladder against v, then type-switches the wrapper
//     and re-enters itself on the inner value with the same Member.
//   - Mutual recursion: walk_members calls dispatch_value on each
//     member's value; dispatch_value calls walk_members when the value
//     is an aggregate.
//   - Member is carried as a std::meta::info NTTP (Stage 12's pattern).
//
// Splice wall note (Stage 9): passing a loop-var reflection through a
// template argument was flaky there. Stage 12's Regex<N> showed
// std::meta::info flows as an NTTP cleanly. This stage leans on that.

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

// ---- Annotation types ----

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}
};

struct MinLength {
    std::size_t value;
    constexpr MinLength(std::size_t v) : value(v) {}
};

struct MinSize {
    std::size_t value;
    constexpr MinSize(std::size_t v) : value(v) {}
};

struct MaxSize {
    std::size_t value;
    constexpr MaxSize(std::size_t v) : value(v) {}
};

struct NotNullopt {};

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

// ---- Dispatch / walk ----
//
// Forward declaration. walk_members and dispatch_value are mutually
// recursive: walk_members visits aggregate members; dispatch_value
// handles one value (running its annotation ladder and unwrapping
// wrappers), calling walk_members back when the value is an aggregate.

template <typename T>
void walk_members(const T& obj, ValidationContext& ctx);

template <std::meta::info Member, typename V>
void dispatch_value(const V& v, ValidationContext& ctx) {
    // Annotation ladder against v. Each branch guards via requires; a
    // mismatch (e.g., Range on string, MinLength on int) silently skips
    // — same fall-through discipline as the closed-set walker.
    template for (constexpr auto ann :
                  std::define_static_array(
                      std::meta::annotations_of(Member)))
    {
        if constexpr (std::meta::type_of(ann) == ^^Range) {
            if constexpr (requires { v < 0LL; }
                       && !is_optional_v<V>
                       && !is_vector_v<V>) {
                constexpr auto r = std::meta::extract<Range>(ann);
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
            if constexpr (requires { v.size(); }
                       && !is_optional_v<V>
                       && !is_vector_v<V>) {
                constexpr auto r = std::meta::extract<MinLength>(ann);
                if (v.size() < r.value) {
                    ctx.errors.push_back({
                        ctx.current_path(),
                        std::format("length must be >= {}, got {}",
                                    r.value, v.size()),
                        "MinLength"
                    });
                }
            }
        } else if constexpr (std::meta::type_of(ann) == ^^MinSize) {
            if constexpr (is_vector_v<V>) {
                constexpr auto r = std::meta::extract<MinSize>(ann);
                if (v.size() < r.value) {
                    ctx.errors.push_back({
                        ctx.current_path(),
                        std::format("size must be >= {}, got {}",
                                    r.value, v.size()),
                        "MinSize"
                    });
                }
            }
        } else if constexpr (std::meta::type_of(ann) == ^^MaxSize) {
            if constexpr (is_vector_v<V>) {
                constexpr auto r = std::meta::extract<MaxSize>(ann);
                if (v.size() > r.value) {
                    ctx.errors.push_back({
                        ctx.current_path(),
                        std::format("size must be <= {}, got {}",
                                    r.value, v.size()),
                        "MaxSize"
                    });
                }
            }
        } else if constexpr (std::meta::type_of(ann) == ^^NotNullopt) {
            if constexpr (is_optional_v<V>) {
                if (!v.has_value()) {
                    ctx.errors.push_back({
                        ctx.current_path(),
                        "must have a value",
                        "NotNullopt"
                    });
                }
            }
        } else if constexpr (std::meta::template_of(
                                 std::meta::type_of(ann)) == ^^Predicate) {
            // Same Stage 16 recipe: type_of → template_arguments_of[0]
            // → splice into a using-alias → extract<Predicate<F>>.
            // The requires-guard below decides whether *this* level is
            // the predicate's apply-site: if F can be called with v
            // and returns bool, fire; otherwise defer to the wrapper
            // recursion where v's type will change.
            constexpr auto targs = std::define_static_array(
                std::meta::template_arguments_of(
                    std::meta::type_of(ann)));
            using F = [:targs[0]:];
            if constexpr (requires(F g) {
                              { g(v) } -> std::same_as<bool>;
                          }) {
                constexpr auto p = std::meta::extract<Predicate<F>>(ann);
                if (!p.f(v)) {
                    ctx.errors.push_back({
                        ctx.current_path(),
                        "custom predicate failed",
                        "Predicate"
                    });
                }
            }
        }
    }

    // Wrapper-piercing recursion. The same Member carries its annotations
    // down so the ladder fires again on the inner value. Guards in the
    // ladder decide which level each annotation applies to.
    if constexpr (is_optional_v<V>) {
        if (v.has_value()) {
            dispatch_value<Member>(*v, ctx);
        }
    } else if constexpr (is_vector_v<V>) {
        for (std::size_t i = 0; i < v.size(); ++i) {
            ctx.path_stack.push_back(i);
            dispatch_value<Member>(v[i], ctx);
            ctx.path_stack.pop_back();
        }
    } else if constexpr (std::is_aggregate_v<V>) {
        walk_members(v, ctx);
    }
}

template <typename T>
void walk_members(const T& obj, ValidationContext& ctx) {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked())))
    {
        ctx.path_stack.push_back(
            std::string{std::meta::identifier_of(member)});
        dispatch_value<member>(obj.[:member:], ctx);
        ctx.path_stack.pop_back();
    }
}

// ---- Demo structs ----

// Q1: single-wrapper inheritance. Scalar annotation on optional<Scalar>
// and vector<Scalar>. Stage 13 and Stage 14 both deferred these because
// the old ladder saw the wrapper and skipped before reaching the inner
// scalar.
struct Q1User {
    [[=Range{0, 150}]]   std::optional<int>         age;
    [[=MinLength{3}]]    std::optional<std::string> nickname;
    [[=Range{0, 150}]]   std::vector<int>           scores;
    [[=MinLength{3}]]    std::vector<std::string>   tags;
};

// Q2: nested composition. vector<optional<Aggregate>> — two wrappers
// plus aggregate recursion, all riding the same dispatch.
struct Address {
    [[=MinLength{2}]]    std::string street;
    [[=Range{1, 99999}]] int         zip_code;
};

struct Q2User {
    std::vector<std::optional<Address>> past_addresses;
};

// Q3: scope selection by callable signature. Two Predicate annotations
// on the *same* vector<int> field. The container-level predicate is
// called once against the whole vector; the element-level predicate is
// called per-index during wrapper recursion. The requires-guard picks
// which fires where.
struct Q3User {
    [[=Predicate{[](const std::vector<int>& v) { return !v.empty(); }},
      =Predicate{[](int x) { return x > 0; }}]]
    std::vector<int> positive_and_nonempty;
};

// Coexistence. Container-level MinSize together with element-level
// Range on the same vector; NotNullopt together with scalar Range on
// the same optional. Both branches of the ladder fire independently
// and errors accumulate.
struct CoexistenceUser {
    [[=MinSize{1}, =Range{0, 150}]]        std::vector<int>   required_ages;
    [[=NotNullopt{}, =Range{0, 150}]]      std::optional<int> required_age;
};

// ---- Helpers ----

void dump(const char* title, const ValidationContext& c) {
    std::cout << "---- " << title << " ----\n";
    if (c.errors.empty()) {
        std::cout << "  (no errors)\n";
    }
    for (const auto& e : c.errors) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }
    std::cout << '\n';
}

int main() {
    // Q1a: wrapper values that fail the inner constraint.
    {
        Q1User u{
            .age       = 200,
            .nickname  = std::string{"al"},
            .scores    = {10, 200, -5},
            .tags      = {"ok", "hi", "fine"},
        };
        ValidationContext c;
        walk_members(u, c);
        dump("Q1a: single-wrapper inheritance, failing", c);
    }

    // Q1b: empty wrappers — no inner value to validate, nothing fires.
    {
        Q1User u{
            .age       = std::nullopt,
            .nickname  = std::nullopt,
            .scores    = {},
            .tags      = {},
        };
        ValidationContext c;
        walk_members(u, c);
        dump("Q1b: empty wrappers, nothing fires", c);
    }

    // Q2: nested composition. vector index + optional unwrap + aggregate
    // walk compose. Missing entries (nullopt) skip the aggregate step.
    {
        Q2User u{
            .past_addresses = {
                Address{.street = "X",  .zip_code = 0},       // both fail
                std::nullopt,                                  // skip
                Address{.street = "OK", .zip_code = 12345},    // pass
                Address{.street = "Y",  .zip_code = 100000},   // both fail
            }
        };
        ValidationContext c;
        walk_members(u, c);
        dump("Q2: nested composition", c);
    }

    // Q3: scope selection by callable signature.
    {
        Q3User u_empty{.positive_and_nonempty = {}};
        ValidationContext c1;
        walk_members(u_empty, c1);
        dump("Q3a: empty vector — container predicate fires", c1);

        Q3User u_mixed{.positive_and_nonempty = {3, -1, 7, 0}};
        ValidationContext c2;
        walk_members(u_mixed, c2);
        dump("Q3b: mixed — element predicate fires per-index", c2);

        Q3User u_ok{.positive_and_nonempty = {1, 2, 3}};
        ValidationContext c3;
        walk_members(u_ok, c3);
        dump("Q3c: all positive, non-empty — no errors", c3);
    }

    // Coexistence. Container + element, or wrapper + scalar, on the same
    // field. Each level of the ladder fires independently.
    {
        CoexistenceUser u_fail{
            .required_ages = {},
            .required_age  = std::nullopt,
        };
        ValidationContext c;
        walk_members(u_fail, c);
        dump("Coexistence a: empty vector + nullopt optional", c);

        CoexistenceUser u_mix{
            .required_ages = {50, 200},
            .required_age  = 200,
        };
        ValidationContext c2;
        walk_members(u_mix, c2);
        dump("Coexistence b: element out-of-range + optional out-of-range", c2);
    }

    return 0;
}
