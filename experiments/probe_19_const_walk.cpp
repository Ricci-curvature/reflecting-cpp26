// Probe 2 for Stage 19: does the walker itself run at constant
// evaluation on clang-p2996?
//
// Probe 1 established that std::format is not yet constexpr in this
// libc++ build, so Stage 19 can't use it at compile time. This probe
// isolates the *other* question: with the format dependency removed
// (annotations push fixed-string messages), does the walker's core
// machinery — `template for` over `nonstatic_data_members_of`,
// reflection splicing, `std::define_static_array`, `std::meta::extract`,
// `ValidationContext::errors.push_back` into a transient `std::vector<
// ValidationError>`, aggregate recursion — run inside a `consteval`
// function?
//
// If yes, Stage 19's path B ("rich messages sacrificed, walker intact")
// is confirmed. The next probe can then investigate whether std::to_chars
// gives us back rich messages within constexpr.
//
// If no, the failure tells us which moving part (vector<string>,
// template for inside consteval, reflection metafunctions at this
// context) doesn't yet cooperate, and Stage 19 scope shrinks accordingly.

#include <meta>
#include <string>
#include <vector>
#include <variant>
#include <cstddef>
#include <type_traits>
#include <concepts>

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

    // current_path without std::format — just join with '.' for field
    // segments; this probe's test data has no indexed paths so the
    // size_t branch isn't exercised (and would need a manual uint→string
    // anyway, which is what probe 3 will investigate via std::to_chars).
    constexpr std::string current_path() const {
        std::string r;
        for (const auto& seg : path_stack) {
            if (std::holds_alternative<std::string>(seg)) {
                const auto& name = std::get<std::string>(seg);
                if (!r.empty()) r += '.';
                r += name;
            }
        }
        return r;
    }
};

// ---- Annotations with fixed messages ----
//
// Shape-identical to the header's versions except the std::format
// calls in validate() are replaced by string literals. Isolates the
// walker question from the formatting question.

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { v < 0LL; }) {
            if (v < min || v > max) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::string{"out of range"},
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
        if constexpr (requires { v.size(); }) {
            if (v.size() < value) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::string{"too short"},
                    std::string{"MinLength"}
                });
            }
        }
    }
};

// ---- Walker ----

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

    if constexpr (std::is_aggregate_v<V>) {
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

// ---- Test fixtures ----

struct Address {
    [[=MinLength{2}]]     std::string street;
    [[=Range{1, 99999}]]  int         zip_code;
};

struct User {
    [[=Range{0, 150}]]   int         age;
    [[=MinLength{3}]]    std::string name;
    Address                          address;
};

// ---- Drivers ----

consteval std::size_t count_errors_bad() {
    User u{200, std::string{"al"}, Address{std::string{"X"}, 0}};
    ValidationContext ctx;
    walk_members(u, ctx);
    return ctx.errors.size();
}

consteval std::size_t count_errors_good() {
    User u{30, std::string{"alice"}, Address{std::string{"Main St"}, 12345}};
    ValidationContext ctx;
    walk_members(u, ctx);
    return ctx.errors.size();
}

// Check path rendering also works end-to-end at consteval — field chain
// through an aggregate, rendered via current_path().
consteval bool nested_path_ok() {
    User u{30, std::string{"alice"}, Address{std::string{"X"}, 0}};
    ValidationContext ctx;
    walk_members(u, ctx);
    if (ctx.errors.size() != 2) return false;
    return ctx.errors[0].path == std::string{"address.street"}
        && ctx.errors[1].path == std::string{"address.zip_code"};
}

// ---- Verification ----

static_assert(count_errors_bad() == 4);
// age, name, address.street, address.zip_code
static_assert(count_errors_good() == 0);
static_assert(nested_path_ok());

int main() {}
