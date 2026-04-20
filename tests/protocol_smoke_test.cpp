// Protocol surface smoke test.
// Exercises the Stage 18 additions through the public <validator.hpp> API:
//   - optional<T> / vector<T> wrapper recursion with the annotation
//     ladder firing at every level
//   - MinSize / MaxSize / NotNullopt
//   - Predicate<F, N> with default and custom message
//   - User-defined protocol annotations (a struct with validate(v, ctx))
//
// Compile:
//   clang++ -std=c++26 -freflection-latest -stdlib=libc++ ... \
//     -I include -o tests/protocol_smoke_test.out tests/protocol_smoke_test.cpp

#include <validator.hpp>

#include <cassert>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// A user-defined protocol annotation. Not known to the library header.
// Rides the same dispatch branch as built-ins because the walker only
// asks: "does this annotation have a validate(v, ctx) member?"
struct StartsWithUppercase {
    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { v.empty(); v[0]; }) {
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

struct Address {
    [[=av::MinLength{2}]]    std::string street;
    [[=av::Range{1, 99999}]] int         zip_code;
};

struct Profile {
    // optional<string> with NotNullopt + user-defined StartsWithUppercase.
    // The ladder fires at the outer level (NotNullopt on optional) and at
    // the inner level (StartsWithUppercase + MinLength on the unwrapped
    // string — those annotations guard themselves out on the optional).
    [[=av::NotNullopt{}, =StartsWithUppercase{}, =av::MinLength{3}]]
    std::optional<std::string> nickname;

    // vector<int> with container-level size bounds + element-level range.
    // MinSize/MaxSize apply at the container level; Range applies per
    // element via the wrapper-piercing recursion.
    [[=av::MinSize{1}, =av::MaxSize{3}, =av::Range{0, 100}]]
    std::vector<int> scores;

    // Predicate with default message (exercises the N=24 deduction guide).
    [[=av::Predicate{[](int x) { return x % 2 == 0; }}]]
    int even_field;

    // Predicate with custom message (exercises the N=literal deduction guide).
    [[=av::Predicate{[](int x) { return x > 0; }, "count must be positive"}]]
    int count;

    // vector<aggregate> — each Address gets walked, its MinLength / Range
    // fire with indexed paths (`past_addresses[0].street`, ...).
    std::vector<Address> past_addresses;
};

static const av::ValidationError*
find(const std::vector<av::ValidationError>& es,
     std::string_view path, std::string_view ann) {
    for (const auto& e : es) {
        if (e.path == path && e.annotation == ann) return &e;
    }
    return nullptr;
}

int main() {
    Profile bad{
        .nickname = std::nullopt,  // NotNullopt fires
        .scores   = {150, -5, 200, 300, 500},  // MaxSize + per-element Range
        .even_field  = 3,   // Predicate fires with default message
        .count       = -1,  // Predicate fires with custom message
        .past_addresses = {
            Address{.street = "X", .zip_code = 0},       // MinLength + Range
            Address{.street = "Main St", .zip_code = 7}, // ok
        },
    };

    auto errors = av::collect(bad);

    // --- outer-level protocol annotations on optional<string> ---
    // NotNullopt fires at the optional level.
    assert(find(errors, "nickname", "NotNullopt"));
    // nullopt means no inner recursion → no MinLength / StartsWithUppercase
    // from this field.
    assert(!find(errors, "nickname", "MinLength"));
    assert(!find(errors, "nickname", "StartsWithUppercase"));

    // --- container-level + element-level on vector<int> ---
    assert(find(errors, "scores", "MaxSize"));       // size 5 > 3
    assert(!find(errors, "scores", "MinSize"));      // size 5 >= 1
    assert(!find(errors, "scores", "Range"));        // vector is guarded
    // Per-element Range fires for every out-of-[0,100] value.
    assert(find(errors, "scores[0]", "Range"));      // 150 > 100
    assert(find(errors, "scores[1]", "Range"));      // -5  < 0
    assert(find(errors, "scores[2]", "Range"));      // 200 > 100
    assert(find(errors, "scores[3]", "Range"));      // 300 > 100
    assert(find(errors, "scores[4]", "Range"));      // 500 > 100

    // --- Predicate default vs custom ---
    const auto* pd = find(errors, "even_field", "Predicate");
    assert(pd);
    assert(pd->message == "custom predicate failed");

    const auto* pc = find(errors, "count", "Predicate");
    assert(pc);
    assert(pc->message == "count must be positive");

    // --- nested aggregate through vector ---
    assert(find(errors, "past_addresses[0].street", "MinLength"));
    assert(find(errors, "past_addresses[0].zip_code", "Range"));
    // Second address is clean.
    assert(find(errors, "past_addresses[1].street", "MinLength") == nullptr);
    assert(find(errors, "past_addresses[1].zip_code", "Range") == nullptr);

    // --- clean profile: no errors ---
    Profile good{
        .nickname = std::string{"Alice"},
        .scores   = {10, 20, 30},
        .even_field = 4,
        .count      = 5,
        .past_addresses = { Address{.street = "Broadway", .zip_code = 10001} },
    };
    assert(av::collect(good).empty());

    std::cout << "protocol smoke test: OK\n";
    return 0;
}
