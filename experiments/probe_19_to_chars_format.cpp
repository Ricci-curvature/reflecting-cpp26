// Probe 3 for Stage 19: can we restore rich messages at constant
// evaluation using std::to_chars, since std::format is not yet
// constexpr in this libc++?
//
// Probe 2 showed the walker itself runs at consteval if we ditch
// std::format. This probe asks whether that's a real loss or a
// swap-out.
//
// std::to_chars for integral types is constexpr in C++23 (P2291), so
// a minimal formatter of the shape
//
//   constexpr std::string int_to_str(long long x);
//   constexpr std::string make_range_msg(long long min, long long max, long long got);
//
// should produce the same "must be in [0, 150], got 200" message the
// runtime walker produces — except constexpr. If so, annotation validate
// bodies can switch from std::format to this mini-formatter and keep
// their messages through Stage 19's compile-time path.
//
// Three checks:
//   (1) std::to_chars in a consteval function, returning a std::string.
//   (2) A composed "make_range_msg(0, 150, 200)" that matches the exact
//       string the runtime walker produces.
//   (3) End-to-end: a Range annotation with a validate() body using the
//       mini-formatter, walked at consteval, message read back from
//       ctx.errors[0].message and compared.

#include <meta>
#include <string>
#include <vector>
#include <variant>
#include <cstddef>
#include <type_traits>
#include <concepts>
#include <charconv>
#include <array>
#include <system_error>

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

constexpr std::string make_range_msg(long long min, long long max, long long got) {
    std::string r;
    r += "must be in [";
    r += int_to_str(min);
    r += ", ";
    r += int_to_str(max);
    r += "], got ";
    r += int_to_str(got);
    return r;
}

constexpr std::string make_min_length_msg(std::size_t want, std::size_t got) {
    std::string r;
    r += "length must be >= ";
    r += uint_to_str(want);
    r += ", got ";
    r += uint_to_str(got);
    return r;
}

// ---- Check (1): to_chars alone ----

consteval std::string t1() { return int_to_str(-42); }
consteval std::string t2() { return uint_to_str(12345); }

static_assert(t1() == std::string{"-42"});
static_assert(t2() == std::string{"12345"});

// ---- Check (2): composed messages ----

consteval std::string t3() { return make_range_msg(0, 150, 200); }
consteval std::string t4() { return make_min_length_msg(3, 2); }

static_assert(t3() == std::string{"must be in [0, 150], got 200"});
static_assert(t4() == std::string{"length must be >= 3, got 2"});

// ---- Check (3): end-to-end through the walker ----

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
            if (std::holds_alternative<std::string>(seg)) {
                const auto& name = std::get<std::string>(seg);
                if (!r.empty()) r += '.';
                r += name;
            } else {
                // Not exercised in this probe's fixture, but ready.
                r += '[';
                r += uint_to_str(std::get<std::size_t>(seg));
                r += ']';
            }
        }
        return r;
    }
};

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { v < 0LL; }) {
            if (v < min || v > max) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    make_range_msg(min, max, static_cast<long long>(v)),
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
                    make_min_length_msg(value, v.size()),
                    std::string{"MinLength"}
                });
            }
        }
    }
};

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

struct User {
    [[=Range{0, 150}]] int         age;
    [[=MinLength{3}]]  std::string name;
};

consteval bool end_to_end() {
    User u{200, std::string{"al"}};
    ValidationContext ctx;
    walk_members(u, ctx);
    if (ctx.errors.size() != 2) return false;
    if (ctx.errors[0].path       != std::string{"age"})                       return false;
    if (ctx.errors[0].message    != std::string{"must be in [0, 150], got 200"}) return false;
    if (ctx.errors[0].annotation != std::string{"Range"})                     return false;
    if (ctx.errors[1].path       != std::string{"name"})                      return false;
    if (ctx.errors[1].message    != std::string{"length must be >= 3, got 2"}) return false;
    if (ctx.errors[1].annotation != std::string{"MinLength"})                 return false;
    return true;
}

static_assert(end_to_end());

int main() {}
