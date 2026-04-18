// Stage 6: Nested struct recursion.
// `User`에 `Address` 필드가 붙고, validate_impl이 class 멤버에 대해 자기 자신을
// 재귀 호출한다. Stage 4에서 만들어둔 path_stack이 여기서 실제로 pay off —
// `address.zip_code` 같은 dotted path가 자동으로 생성된다.
//
// 재귀 dispatch 규칙: std::is_aggregate_v<MT>.
//  - Address (plain struct, constructor 없음) → aggregate → 재귀한다.
//  - std::string (constructor/private member 있음) → non-aggregate → leaf로 본다.
//  - int 같은 scalar → class type이 아니라서 애초에 재귀 대상 아님.
//
// 대안으로 is_class_v를 썼다면 std::string까지 재귀해서 내부 멤버(_M_dataplus 등)로
// 파고들었을 것이다. Stage 6 스코프는 "사용자 정의 plain struct만" 재귀다.
//
// Stage 5에서 기록한 clang-p2996 quirk 대비용 `if constexpr (requires { ... })`
// 가드는 모든 annotation 분기에 일괄 적용.

#include <meta>
#include <iostream>
#include <string>
#include <vector>
#include <format>
#include <cstddef>
#include <type_traits>

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

struct Address {
    [[=MinLength{2}]] std::string street;
    [[=Range{1, 99999}]] int zip_code;
};

struct User {
    [[=Range{0, 150}]] int age;
    [[=MinLength{3}]] [[=MaxLength{32}]] std::string name;
    [[=NotEmpty{}]] std::string email;
    Address address;
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
                if constexpr (requires { obj.[:member:] < 0LL; }) {
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

        // Recurse into aggregate (plain struct) members. std::string 등 non-aggregate는
        // 여기서 자동으로 걸러진다.
        using MT = std::remove_cvref_t<decltype(obj.[:member:])>;
        if constexpr (std::is_aggregate_v<MT>) {
            validate_impl(obj.[:member:], ctx);
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
    User good{30, "alice", "a@b.c", {"123 Main St", 12345}, 0};
    User bad_zip{30, "alice", "a@b.c", {"123 Main St", 0}, 0};
    User bad_street{30, "alice", "a@b.c", {"X", 12345}, 0};
    User multi_fail{200, "al", "", {"X", 0}, 0};

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
    demo("bad_zip:    ", bad_zip);
    demo("bad_street: ", bad_street);
    demo("multi_fail: ", multi_fail);
    return 0;
}
