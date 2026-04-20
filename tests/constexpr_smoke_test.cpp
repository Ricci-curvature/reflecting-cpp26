// Stage 19 smoke test: compile-time validation through the public API.
//
// Covers:
//   1. passes(obj) / first_error(obj) at consteval on literal structs.
//   2. static_assert(passes(obj), first_error(obj)) compiling clean
//      when the struct is valid.
//   3. Byte-identical messages at consteval and runtime (to_str formatter
//      matches std::format output the runtime path produces).
//   4. vector-containing types via a consteval helper (no namespace-scope
//      transient heap).
//   5. User-defined annotation participating in the compile-time path —
//      the protocol is open, not gated by the built-in annotation set.

#include "validator.hpp"

#include <iostream>
#include <string>
#include <vector>

// ---- Demo types ----

struct Address {
    [[=av::MinLength{2}]]     std::string street;
    [[=av::Range{1, 99999}]]  int         zip_code;
};

struct User {
    [[=av::Range{0, 150}]]  int         age;
    [[=av::MinLength{3}]]   std::string name;
    Address                             address;
};

struct Team {
    [[=av::MinSize{1}]]  std::vector<User> members;
};

// User-defined annotation — consumed by the library's protocol without
// any header change.
struct StartsWithUppercase {
    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { v.size(); v[0]; }
                   && !av::detail::is_optional_v<V>
                   && !av::detail::is_vector_v<V>) {
            if (v.empty() || v[0] < 'A' || v[0] > 'Z') {
                ctx.errors.push_back({
                    ctx.current_path(),
                    "must start with uppercase letter",
                    "StartsWithUppercase"
                });
            }
        }
    }
};

struct Account {
    [[=StartsWithUppercase{}, =av::MinLength{3}]]  std::string username;
    [[=av::Range{0, 120}]]                         int         age;
};

// ---- Compile-time checks ----
//
// Style A: namespace-scope constexpr object for SSO-sized members.
constexpr User alice{
    .age = 30,
    .name = std::string{"Alice"},
    .address = Address{.street = std::string{"Main St"},
                       .zip_code = 12345}
};

static_assert(av::passes(alice), av::first_error(alice));
static_assert(av::first_error(alice).empty());

constexpr Account good_account{
    .username = std::string{"Root"},
    .age = 42
};
static_assert(av::passes(good_account), av::first_error(good_account));

// Style B: consteval wrapper for vector-containing types.
consteval bool team_passes_ct() {
    Team t{
        .members = std::vector<User>{
            User{.age = 30, .name = std::string{"Alice"},
                 .address = Address{.street = std::string{"Main St"},
                                    .zip_code = 12345}},
            User{.age = 45, .name = std::string{"Bob"},
                 .address = Address{.street = std::string{"Broadway"},
                                    .zip_code = 10001}},
        },
    };
    return av::passes(t);
}

consteval std::string team_first_error_ct() {
    Team t{
        .members = std::vector<User>{
            User{.age = 30, .name = std::string{"Alice"},
                 .address = Address{.street = std::string{"Main St"},
                                    .zip_code = 12345}},
            User{.age = 45, .name = std::string{"Bob"},
                 .address = Address{.street = std::string{"Broadway"},
                                    .zip_code = 10001}},
        },
    };
    return av::first_error(t);
}

static_assert(team_passes_ct(), team_first_error_ct());

// Message format is exercised via consteval helper + string comparison,
// since a passing static_assert never surfaces its message argument.
consteval std::string bad_user_first_error() {
    User u{.age = 200, .name = std::string{"al"},
           .address = Address{.street = std::string{"X"}, .zip_code = 0}};
    return av::first_error(u);
}

static_assert(bad_user_first_error()
              == std::string{"age: must be in [0, 150], got 200 (Range)"});

consteval std::string bad_min_length_first_error() {
    User u{.age = 30, .name = std::string{"al"},
           .address = Address{.street = std::string{"Main St"},
                              .zip_code = 12345}};
    return av::first_error(u);
}

static_assert(bad_min_length_first_error()
              == std::string{"name: length must be >= 3, got 2 (MinLength)"});

consteval std::string empty_team_first_error() {
    Team t{.members = {}};
    return av::first_error(t);
}

static_assert(empty_team_first_error()
              == std::string{"members: size must be >= 1, got 0 (MinSize)"});

consteval std::string bad_user_annotation_first_error() {
    Account a{.username = std::string{"root"}, .age = 42};
    return av::first_error(a);
}

static_assert(bad_user_annotation_first_error()
              == std::string{"username: must start with uppercase letter "
                             "(StartsWithUppercase)"});

// ---- Runtime driver: same walker, byte-identical output ----

int main() {
    // Sanity: compile-time accessors also work at runtime.
    if (!av::passes(alice)) {
        std::cout << "runtime alice unexpectedly failed: "
                  << av::first_error(alice) << '\n';
        return 1;
    }

    // Runtime first_error on a bad user must match the exact string the
    // static_assert above pins the consteval path to. Any drift between
    // to_str and std::format output would break either this check or the
    // static_assert — whichever was compiled.
    User bad{.age = 200, .name = std::string{"al"},
             .address = Address{.street = std::string{"X"}, .zip_code = 0}};
    const std::string rt_msg = av::first_error(bad);
    const char* expected = "age: must be in [0, 150], got 200 (Range)";
    if (rt_msg != expected) {
        std::cout << "runtime first_error mismatch:\n"
                  << "  got:      " << rt_msg  << '\n'
                  << "  expected: " << expected << '\n';
        return 1;
    }

    std::cout << "constexpr smoke test: OK\n";
    return 0;
}
