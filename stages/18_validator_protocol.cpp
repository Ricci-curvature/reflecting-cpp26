// Stage 18: Validator protocol — annotation IS the validator.
//
// Stage 16 opened the closed-set dispatch with one wrapper branch:
// Predicate<F> let users bring a captureless lambda, and the walker's
// dispatch ladder gained a single generic slot at the end of the chain.
// Stage 17 made that wrapper branch compose through nesting — the
// annotation ladder travels through optional / vector wrappers and
// fires at whichever level its requires-guard applies.
//
// Stage 18 takes the next step on the same axis: instead of a fixed
// wrapper (Predicate<F>), the walker dispatches on a *protocol* —
// "does this annotation have a constexpr validate(v, ctx) member?" Any
// struct satisfying that shape rides through [[=X{}]] syntax and fires
// at its own self-determined apply-site.
//
// Consequences:
//   - User-defined validators: StartsWithUppercase, MustBePositive, and
//     the like. No wrapper, no template parameter gymnastics — the user
//     writes a struct with a validate() member and attaches it.
//   - The closed-set dispatch collapses. Range / MinLength / MinSize /
//     MaxSize / NotNullopt all gain validate() members and move onto
//     the protocol path. The Stage 17 ladder of `type_of(ann) == ^^X`
//     branches shrinks to **one** branch: the protocol call.
//   - Predicate<F> becomes Predicate<F, N> with a char[N] message field,
//     echoing Stage 12's Regex<N> NTTP pattern. The custom-message
//     customization deferred from Stage 17 lands as a drop-in.
//
// Three probes (experiments/probe_q2_*.cpp, probe_q13_*.cpp,
// probe_identifier_of.cpp) established the ground rules before this
// file:
//   - Annotation structs with template member functions are structural
//     as NTTPs (Q2).
//   - requires { ann.validate(v, ctx); } is stable as a template-for
//     guard with mixed protocol / legacy annotations (Q1, Q3).
//   - identifier_of on class-template specializations is consteval-
//     rejected, so annotation names stay hardcoded inside each
//     validate() body rather than being auto-extracted.
//
// Architecture (diff from Stage 17):
//   - walk_members<T> unchanged.
//   - dispatch_value<Member, V>'s annotation ladder is one branch:
//       if constexpr (requires { a.validate(v, ctx); }) a.validate(v, ctx);
//     Wrapper-piercing recursion (optional / vector / aggregate)
//     unchanged — same design as Stage 17, decoupled from the ladder.
//   - Every annotation owns its applicability guards inside validate()
//     via if constexpr + requires + is_optional_v / is_vector_v.

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

// ---- Type traits (shared by annotations) ----

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

// ---- Annotations: all protocol-based ----
//
// Each annotation owns two things:
//   1. Its payload (min/max, length bound, predicate callable, ...).
//   2. A constexpr template validate(const V&, Ctx&) member that
//      decides its own apply-site via requires + is_optional_v /
//      is_vector_v guards, and pushes ValidationError on failure.
//
// The walker's dispatch has no knowledge of any specific annotation —
// it just asks "can you validate?" and calls through.

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { v < 0LL; }
                   && !is_optional_v<V>
                   && !is_vector_v<V>) {
            if (v < min || v > max) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::format("must be in [{}, {}], got {}", min, max, v),
                    "Range"
                });
            }
        }
    }
};

struct MinLength {
    std::size_t value;
    constexpr MinLength(std::size_t v) : value(v) {}

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { v.size(); }
                   && !is_optional_v<V>
                   && !is_vector_v<V>) {
            if (v.size() < value) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::format("length must be >= {}, got {}",
                                value, v.size()),
                    "MinLength"
                });
            }
        }
    }
};

struct MinSize {
    std::size_t value;
    constexpr MinSize(std::size_t v) : value(v) {}

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (is_vector_v<V>) {
            if (v.size() < value) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::format("size must be >= {}, got {}",
                                value, v.size()),
                    "MinSize"
                });
            }
        }
    }
};

struct MaxSize {
    std::size_t value;
    constexpr MaxSize(std::size_t v) : value(v) {}

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (is_vector_v<V>) {
            if (v.size() > value) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::format("size must be <= {}, got {}",
                                value, v.size()),
                    "MaxSize"
                });
            }
        }
    }
};

struct NotNullopt {
    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (is_optional_v<V>) {
            if (!v.has_value()) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    "must have a value",
                    "NotNullopt"
                });
            }
        }
    }
};

// Predicate<F, N>: Stage 17's wrapper + an N-sized char array for a
// custom error message, matching the deferred-from-Stage-17 promise.
// Two deduction guides:
//   Predicate{f}            → Predicate<F, 24> with default message
//   Predicate{f, "..."}     → Predicate<F, N>  with the literal's length
template <typename F, std::size_t N = 24>
struct Predicate {
    F f;
    char message[N] = "custom predicate failed";

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { { f(v) } -> std::same_as<bool>; }) {
            if (!f(v)) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::string{message},
                    "Predicate"
                });
            }
        }
    }
};

template <typename F>
Predicate(F) -> Predicate<F, 24>;

template <typename F, std::size_t N>
Predicate(F, const char (&)[N]) -> Predicate<F, N>;

// ---- User-defined protocol annotations (demo) ----
//
// Pure user code — nothing in the library header knows about these.
// They ride the same protocol branch as Range / MinLength / Predicate.

struct StartsWithUppercase {
    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires {
                          { v.empty() } -> std::convertible_to<bool>;
                          v[0];
                      }) {
            if (v.empty() || v[0] < 'A' || v[0] > 'Z') {
                ctx.errors.push_back({
                    ctx.current_path(),
                    "must start with an uppercase letter",
                    "StartsWithUppercase"
                });
            }
        }
    }
};

struct MustBePositive {
    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { v > 0; }
                   && !is_optional_v<V>
                   && !is_vector_v<V>) {
            if (!(v > 0)) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::format("must be positive, got {}", v),
                    "MustBePositive"
                });
            }
        }
    }
};

// ---- Dispatch / walk ----
//
// The annotation ladder is one branch: the protocol call. Every
// annotation — built-in or user-defined — goes through the same path.
// Wrapper-piercing recursion is unchanged from Stage 17.

template <typename T>
void walk_members(const T& obj, ValidationContext& ctx);

template <std::meta::info Member, typename V>
void dispatch_value(const V& v, ValidationContext& ctx) {
    template for (constexpr auto ann :
                  std::define_static_array(
                      std::meta::annotations_of(Member)))
    {
        using A = [:std::meta::type_of(ann):];
        constexpr auto a = std::meta::extract<A>(ann);
        if constexpr (requires { a.validate(v, ctx); }) {
            a.validate(v, ctx);
        }
    }

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

// P1: user-defined protocol annotations coexist with built-ins on the
// same struct. No wrapper, no library change — the walker picks them
// up through the single protocol branch.
struct P1User {
    [[=StartsWithUppercase{}, =MinLength{3}]] std::string name;
    [[=MustBePositive{}, =Range{0, 150}]]     int         age;
};

// P2: Predicate with default message vs custom message. Default form
// still works (N defaults to 24, message defaults to "custom predicate
// failed"); the string-literal overload picks a matching N through the
// deduction guide.
struct P2User {
    [[=Predicate{[](int x) { return x % 2 == 0; }}]]
    int default_msg;

    [[=Predicate{[](int x) { return x > 0; }, "count must be positive"}]]
    int custom_msg;
};

// P3: protocol carries through wrapper recursion the same way Stage 17
// did — user-defined annotation on optional<string>, vector<int>.
struct P3User {
    [[=StartsWithUppercase{}]] std::optional<std::string> title;
    [[=MustBePositive{}]]      std::vector<int>           scores;
};

// P4: nested composition still works (Stage 17 Q2 equivalent, now via
// protocol). vector<optional<Aggregate>> with annotated inner fields.
struct Address {
    [[=MinLength{2}]]    std::string street;
    [[=Range{1, 99999}]] int         zip_code;
};

struct P4User {
    std::vector<std::optional<Address>> past_addresses;
};

// P5: signature-selected scope (Stage 17 Q3 equivalent). Two Predicate
// annotations on one vector<int> field, one container-level, one
// element-level — fires at the right scope from the requires-guard.
struct P5User {
    [[=Predicate{[](const std::vector<int>& v) { return !v.empty(); },
                 "list must be non-empty"},
      =Predicate{[](int x) { return x > 0; },
                 "element must be positive"}]]
    std::vector<int> entries;
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
    // P1: user-defined + built-in coexisting.
    {
        P1User u{.name = "al", .age = 200};
        ValidationContext c;
        walk_members(u, c);
        dump("P1: user-defined + built-in on same field", c);
    }

    // P2: default message vs custom message.
    {
        P2User u{.default_msg = 3, .custom_msg = -5};
        ValidationContext c;
        walk_members(u, c);
        dump("P2: Predicate default vs custom message", c);
    }

    // P3: protocol through wrapper recursion.
    {
        P3User u{
            .title  = std::string{"lowercase title"},
            .scores = {3, -1, 0, 7},
        };
        ValidationContext c;
        walk_members(u, c);
        dump("P3: protocol + wrapper recursion", c);
    }

    // P4: nested composition (vector<optional<aggregate>>).
    {
        P4User u{
            .past_addresses = {
                Address{.street = "X",  .zip_code = 0},
                std::nullopt,
                Address{.street = "OK", .zip_code = 12345},
                Address{.street = "Y",  .zip_code = 100000},
            },
        };
        ValidationContext c;
        walk_members(u, c);
        dump("P4: nested composition via protocol", c);
    }

    // P5: signature-selected scope — two predicates on one field.
    {
        P5User u_empty{.entries = {}};
        ValidationContext c1;
        walk_members(u_empty, c1);
        dump("P5a: empty — container predicate fires", c1);

        P5User u_mixed{.entries = {3, -1, 7, 0}};
        ValidationContext c2;
        walk_members(u_mixed, c2);
        dump("P5b: mixed — element predicate fires per-index", c2);

        P5User u_ok{.entries = {1, 2, 3}};
        ValidationContext c3;
        walk_members(u_ok, c3);
        dump("P5c: all positive, non-empty — no errors", c3);
    }

    return 0;
}
