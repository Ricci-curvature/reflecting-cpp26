// Stage 3: First validator.
// Public API는 bool. 내부는 이미 수집 기반 (++failures, early return 없음).
// Stage 4에서 ++failures 자리가 errors.push_back(...)로 바뀌어도
// 제어 흐름은 그대로 유지되도록 의도한 구조.

#include <meta>
#include <iostream>
#include <cstddef>

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}
};

struct User {
    [[=Range{0, 150}]] int age;
    [[=Range{1, 1'000'000}]] int id;
    int unrelated;  // annotation 없는 필드 — 실패에 기여 안 함
};

namespace detail {

template <typename T>
constexpr std::size_t validate_impl(const T& obj) {
    std::size_t failures = 0;

    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked())))
    {
        template for (constexpr auto ann :
                      std::define_static_array(
                          std::meta::annotations_of(member)))
        {
            if constexpr (std::meta::type_of(ann) == ^^Range) {
                constexpr auto r = std::meta::extract<Range>(ann);
                auto v = obj.[:member:];
                if (v < r.min || v > r.max) {
                    ++failures;
                    // early return 하지 않음 — 수집 철학 유지.
                }
            }
        }
    }

    return failures;
}

}  // namespace detail

template <typename T>
constexpr bool validate(const T& obj) {
    return detail::validate_impl(obj) == 0;
}

int main() {
    User good{30, 42, 999};
    User one_fail{200, 42, 999};   // age 범위 초과
    User two_fail{200, -1, 999};   // age + id 둘 다 범위 밖

    std::cout << std::boolalpha;
    std::cout << "good:     validate=" << validate(good)
              << ", failures=" << detail::validate_impl(good) << '\n';
    std::cout << "one_fail: validate=" << validate(one_fail)
              << ", failures=" << detail::validate_impl(one_fail) << '\n';
    std::cout << "two_fail: validate=" << validate(two_fail)
              << ", failures=" << detail::validate_impl(two_fail) << '\n';
    return 0;
}
