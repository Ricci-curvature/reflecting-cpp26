// Stage 5: String annotations.
// MinLength / MaxLength (literal class with data) + NotEmpty (tag type).
// What Stage 2 proved — "both annotation shapes flow through the same
// pipeline" — lands in the real validator engine here.
// Regex is deferred: working around the literal-class restriction needs a
// separate technique that doesn't belong in this stage.
//
// clang-p2996 quirk: inside `template for`, an `if constexpr (type_of(ann)
// == ^^X)` branch discards unreliably on reflection-dependent conditions.
// Through Stage 4 there was only one branch (Range), so the bug stayed
// hidden. Once multiple branches appear and each branch body uses a
// different expression per member type (.size() / .empty()), the MinLength
// branch body typechecks against an int member and fails with
// "const int has no .size()". Fix: wrap each branch body in another
// `if constexpr (requires { ... })` guard — even if the outer discard
// leaks, the inner requires catches it.

#include <meta>
#include <iostream>
#include <string>
#include <vector>
#include <format>
#include <cstddef>

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}
};

struct MinLength {
    std::size_t value;
    constexpr MinLength(std::size_t v) : value(v) {}
};

struct MaxLength {
    std::size_t value;
    constexpr MaxLength(std::size_t v) : value(v) {}
};

struct NotEmpty {};

struct User {
    [[=Range{0, 150}]] int age;
    [[=MinLength{3}]] [[=MaxLength{32}]] std::string name;
    [[=NotEmpty{}]] std::string email;
    int unrelated;
};

struct ValidationError {
    std::string path;
    std::string message;
    std::string annotation;
};

struct ValidationContext {
    std::vector<ValidationError> errors;
    std::vector<std::string> path_stack;

    std::string current_path() const {
        std::string result;
        for (std::size_t i = 0; i < path_stack.size(); ++i) {
            if (i > 0) result += '.';
            result += path_stack[i];
        }
        return result;
    }
};

namespace detail {

template <typename T>
void validate_impl(const T& obj, ValidationContext& ctx) {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked())))
    {
        ctx.path_stack.push_back(
            std::string{std::meta::identifier_of(member)});

        template for (constexpr auto ann :
                      std::define_static_array(
                          std::meta::annotations_of(member)))
        {
            if constexpr (std::meta::type_of(ann) == ^^Range) {
                constexpr auto r = std::meta::extract<Range>(ann);
                auto v = obj.[:member:];
                if (v < r.min || v > r.max) {
                    ctx.errors.push_back({
                        ctx.current_path(),
                        std::format("must be in [{}, {}], got {}", r.min, r.max, v),
                        "Range"
                    });
                }
            } else if constexpr (std::meta::type_of(ann) == ^^MinLength) {
                if constexpr (requires { obj.[:member:].size(); }) {
                    constexpr auto r = std::meta::extract<MinLength>(ann);
                    const auto& v = obj.[:member:];
                    if (v.size() < r.value) {
                        ctx.errors.push_back({
                            ctx.current_path(),
                            std::format("length must be >= {}, got {}", r.value, v.size()),
                            "MinLength"
                        });
                    }
                }
            } else if constexpr (std::meta::type_of(ann) == ^^MaxLength) {
                if constexpr (requires { obj.[:member:].size(); }) {
                    constexpr auto r = std::meta::extract<MaxLength>(ann);
                    const auto& v = obj.[:member:];
                    if (v.size() > r.value) {
                        ctx.errors.push_back({
                            ctx.current_path(),
                            std::format("length must be <= {}, got {}", r.value, v.size()),
                            "MaxLength"
                        });
                    }
                }
            } else if constexpr (std::meta::type_of(ann) == ^^NotEmpty) {
                if constexpr (requires { obj.[:member:].empty(); }) {
                    const auto& v = obj.[:member:];
                    if (v.empty()) {
                        ctx.errors.push_back({
                            ctx.current_path(),
                            "must not be empty",
                            "NotEmpty"
                        });
                    }
                }
            }
        }

        ctx.path_stack.pop_back();
    }
}

}  // namespace detail

template <typename T>
std::vector<ValidationError> collect(const T& obj) {
    ValidationContext ctx;
    detail::validate_impl(obj, ctx);
    return std::move(ctx.errors);
}

template <typename T>
bool validate(const T& obj) {
    return collect(obj).empty();
}

std::string format_error(const ValidationError& e) {
    return std::format("{}: {} ({})", e.path, e.message, e.annotation);
}

int main() {
    User good{30, "alice", "a@b.c", 0};
    User short_name{30, "al", "a@b.c", 0};
    User long_name{30, std::string(33, 'x'), "a@b.c", 0};
    User empty_email{30, "alice", "", 0};
    User multi_fail{200, "al", "", 0};

    auto demo = [](const char* label, const User& u) {
        auto errors = collect(u);
        std::cout << label
                  << " validate=" << std::boolalpha << validate(u)
                  << ", errors=" << errors.size() << '\n';
        for (const auto& e : errors) {
            std::cout << "  " << format_error(e) << '\n';
        }
    };

    demo("good:       ", good);
    demo("short_name: ", short_name);
    demo("long_name:  ", long_name);
    demo("empty_email:", empty_email);
    demo("multi_fail: ", multi_fail);
    return 0;
}
