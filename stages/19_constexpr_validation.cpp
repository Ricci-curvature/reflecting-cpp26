// Stage 19: Constexpr validation — static_assert on literal structs.
//
// Four probes (experiments/probe_19_*) established the ground:
//   1. std::format is NOT yet constexpr in this libc++ build. Rich
//      messages at compile time can't use it.
//   2. The walker's core — template for over nonstatic_data_members_of,
//      reflection splices, define_static_array, extract, and a
//      ValidationContext with transient std::vector<ValidationError> —
//      runs at consteval without incident.
//   3. std::to_chars IS constexpr (P2291, C++23) for integrals. A
//      mini-formatter on top of it produces messages byte-identical
//      to the runtime std::format path.
//   4. static_assert accepts a constexpr std::string as its message
//      (P2741, C++26), and the compiler embeds the computed string
//      in the diagnostic. Stage 19 can surface per-annotation errors
//      inline:
//
//        error: static assertion failed: age: must be in [0, 150], got 200 (Range)
//
// Together those four give Stage 19 the shape:
//
//   - Every annotation's validate() body swaps std::format(...) for
//     manual string concatenation + to_str(...). Byte-identical output.
//   - walk_members / dispatch_value gain constexpr.
//   - Two new entry points:
//       constexpr bool        passes(const T&)
//       constexpr std::string first_error(const T&)
//   - Usage: static_assert(passes(u), first_error(u));
//
// The runtime path is preserved — passes/first_error also work at
// runtime, and the original walker output is unchanged byte-for-byte.

#include <meta>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <optional>
#include <cstddef>
#include <type_traits>
#include <concepts>
#include <charconv>

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

// ---- Mini-formatter (probe 3) ----
//
// Enough integral formatting for every built-in annotation's messages.
// std::to_chars returns (ptr, ec); we construct a std::string from
// [buf, ptr). All operations constexpr.

constexpr std::string to_str(long long x) {
    char buf[24];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), x);
    return std::string(buf, ptr);
}

constexpr std::string to_str(std::size_t x) {
    char buf[24];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), x);
    return std::string(buf, ptr);
}

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

    constexpr std::string current_path() const {
        std::string r;
        for (const auto& seg : path_stack) {
            if (std::holds_alternative<std::size_t>(seg)) {
                r += '[';
                r += to_str(std::get<std::size_t>(seg));
                r += ']';
            } else {
                const auto& name = std::get<std::string>(seg);
                if (!r.empty()) r += '.';
                r += name;
            }
        }
        return r;
    }
};

// ---- Annotations (protocol-based, same shape as Stage 18; messages
//      composed via to_str so they work at consteval) ----

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { v < 0LL; }
                   && !is_optional_v<V>
                   && !is_vector_v<V>) {
            if (v < min || v > max) {
                std::string msg;
                msg += "must be in [";
                msg += to_str(min);
                msg += ", ";
                msg += to_str(max);
                msg += "], got ";
                msg += to_str(static_cast<long long>(v));
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::move(msg),
                    std::string{"Range"}
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
                std::string msg;
                msg += "length must be >= ";
                msg += to_str(value);
                msg += ", got ";
                msg += to_str(v.size());
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::move(msg),
                    std::string{"MinLength"}
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
                std::string msg;
                msg += "size must be >= ";
                msg += to_str(value);
                msg += ", got ";
                msg += to_str(v.size());
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::move(msg),
                    std::string{"MinSize"}
                });
            }
        }
    }
};

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
                    std::string{"Predicate"}
                });
            }
        }
    }
};

template <typename F>
Predicate(F) -> Predicate<F, 24>;

template <typename F, std::size_t N>
Predicate(F, const char (&)[N]) -> Predicate<F, N>;

// ---- Walker (constexpr on both layers) ----

template <typename T>
constexpr void walk_members(const T& obj, ValidationContext& ctx);

template <std::meta::info Member, typename V>
constexpr void dispatch_value(const V& v, ValidationContext& ctx) {
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
constexpr void walk_members(const T& obj, ValidationContext& ctx) {
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

// ---- Compile-time entry points ----
//
// passes(obj)      — true iff no annotation rejected.
// first_error(obj) — "<path>: <message> (<annotation>)" or empty.
//
// Both constexpr; both work at runtime as well.

template <typename T>
constexpr bool passes(const T& obj) {
    ValidationContext ctx;
    walk_members(obj, ctx);
    return ctx.errors.empty();
}

template <typename T>
constexpr std::string first_error(const T& obj) {
    ValidationContext ctx;
    walk_members(obj, ctx);
    if (ctx.errors.empty()) return {};
    const auto& e = ctx.errors.front();
    std::string r;
    r += e.path;
    r += ": ";
    r += e.message;
    r += " (";
    r += e.annotation;
    r += ")";
    return r;
}

// ---- Demo types ----

struct Address {
    [[=MinLength{2}]]     std::string street;
    [[=Range{1, 99999}]]  int         zip_code;
};

struct User {
    [[=Range{0, 150}]]  int         age;
    [[=MinLength{3}]]   std::string name;
    Address                         address;
};

struct Team {
    [[=MinSize{1}]]  std::vector<User> members;
};

// ---- Compile-time checks ----
//
// Two styles:
//
//   (A) Namespace-scope `constexpr Struct u{...};` for types whose
//       members fit SSO — their state has no heap remnant past the
//       initializer. Here `alice.name = "alice"` (5 chars) and
//       `alice.address.street = "Main St"` (7 chars) both fit SSO on
//       libc++.
//
//   (B) consteval helper that constructs and walks locally, for types
//       with `std::vector<T>` members — vectors always allocate, so
//       they can't live in a namespace-scope constexpr (no transient
//       heap allowed past the initializer). Keep construction + walk
//       inside the consteval scope; return bool / string.

// Style (A).
constexpr User alice{
    .age = 30,
    .name = std::string{"alice"},
    .address = Address{.street = std::string{"Main St"},
                       .zip_code = 12345}
};

static_assert(passes(alice), first_error(alice));

// Style (B) — Team has vector<User>, wrap in consteval.
consteval bool team_passes() {
    Team t{
        .members = std::vector<User>{
            alice,
            User{.age = 45, .name = std::string{"bob"},
                 .address = Address{.street = std::string{"Broadway"},
                                    .zip_code = 10001}},
        },
    };
    return passes(t);
}

consteval std::string team_err() {
    Team t{
        .members = std::vector<User>{
            alice,
            User{.age = 45, .name = std::string{"bob"},
                 .address = Address{.street = std::string{"Broadway"},
                                    .zip_code = 10001}},
        },
    };
    return first_error(t);
}

static_assert(team_passes(), team_err());

// Uncomment any one of these to observe the compiler diagnostic.
// Expected outputs:
//
//   error: static assertion failed: age: must be in [0, 150], got 200 (Range)
//   error: static assertion failed: name: length must be >= 3, got 2 (MinLength)
//   error: static assertion failed: members: size must be >= 1, got 0 (MinSize)
//
// constexpr User bad_age{.age = 200, .name = std::string{"alice"},
//                        .address = Address{.street = std::string{"Main St"}, .zip_code = 12345}};
// static_assert(passes(bad_age), first_error(bad_age));
//
// constexpr User bad_name{.age = 30, .name = std::string{"al"},
//                         .address = Address{.street = std::string{"Main St"}, .zip_code = 12345}};
// static_assert(passes(bad_name), first_error(bad_name));
//
// consteval bool empty_team_passes() {
//     Team t{.members = {}};
//     return passes(t);
// }
// consteval std::string empty_team_err() {
//     Team t{.members = {}};
//     return first_error(t);
// }
// static_assert(empty_team_passes(), empty_team_err());

// ---- Runtime driver: same walker, byte-identical output ----

int main() {
    // Good: 0 errors.
    {
        User u{30, std::string{"alice"},
               Address{std::string{"Main St"}, 12345}};
        ValidationContext ctx;
        walk_members(u, ctx);
        std::cout << "[runtime] good user: " << ctx.errors.size()
                  << " error(s)\n";
    }

    // Bad: 4 errors (age, name, address.street, address.zip_code).
    {
        User u{200, std::string{"al"},
               Address{std::string{"X"}, 0}};
        ValidationContext ctx;
        walk_members(u, ctx);
        std::cout << "[runtime] bad user: " << ctx.errors.size()
                  << " error(s)\n";
        for (const auto& e : ctx.errors) {
            std::cout << "  " << e.path << ": " << e.message
                      << " (" << e.annotation << ")\n";
        }
    }

    std::cout << "[compile-time] alice passes: "
              << std::boolalpha << passes(alice) << '\n';
    std::cout << "[compile-time] team_passes(): "
              << team_passes() << '\n';

    return 0;
}
