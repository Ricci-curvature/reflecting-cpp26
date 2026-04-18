// Stage 4: Collection-based core with structured errors + path tracking.
//
// Stage 3의 ++failures 자리가 ctx.errors.push_back(...)로 치환됐고, 제어 흐름은
// 그대로다. path_stack push/pop으로 "어느 필드에서 에러가 났는지"를 저장한다.
// 중첩 struct 재귀는 Stage 6 스코프 — 여기선 flat struct만.

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

// 드라이버 쪽 포맷 헬퍼. 설계 문서 스펙: <path>: <message> (<annotation>)
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
