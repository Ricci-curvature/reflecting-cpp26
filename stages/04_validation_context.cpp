// Stage 4: Collection-based core with structured errors + path tracking.
//
// The ++failures line from Stage 3 is swapped for ctx.errors.push_back(...),
// and the control flow is unchanged. path_stack push/pop records "which field
// produced the error". Nested-struct recursion is Stage 6 scope — flat structs
// only here.

#include <meta>
#include <iostream>
#include <string>
#include <vector>
#include <format>

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}
};

struct User {
    [[=Range{0, 150}]] int age;
    [[=Range{1, 1'000'000}]] int id;
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

// Driver-side format helper. Spec from the design doc: <path>: <message> (<annotation>)
std::string format_error(const ValidationError& e) {
    return std::format("{}: {} ({})", e.path, e.message, e.annotation);
}

int main() {
    User good{30, 42, 999};
    User one_fail{200, 42, 999};
    User two_fail{200, -1, 999};

    auto demo = [](const char* label, const User& u) {
        auto errors = collect(u);
        std::cout << label
                  << " validate=" << std::boolalpha << validate(u)
                  << ", errors=" << errors.size() << '\n';
        for (const auto& e : errors) {
            std::cout << "  " << format_error(e) << '\n';
        }
    };

    demo("good:    ", good);
    demo("one_fail:", one_fail);
    demo("two_fail:", two_fail);
    return 0;
}
