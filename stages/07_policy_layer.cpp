// Stage 7: Policy layer.
// 코어(validate_impl)는 여전히 수집 기반. 정책 3종과 FailFast 모드만 얹는다.
//   - collect(obj, mode)  → std::vector<ValidationError>
//   - check(obj, mode)    → std::expected<void, std::vector<ValidationError>>
//   - validate(obj, mode) → void, 실패 시 ValidationException throw
// Fail-fast는 정책 레이어가 아니라 ValidationContext.mode가 결정한다.
// validate_impl 본체는 Stage 6에서 바뀐 게 없고, 각 iteration 진입 앞에
// `if (!ctx.should_stop())` 런타임 가드 한 줄씩만 삽입한다.
// 이 분리가 깨지면 코어가 세 정책 각각의 제어 흐름을 알아야 한다 — 그걸 피하는 게
// Stage 3부터 의도한 구조다.

#include <meta>
#include <iostream>
#include <string>
#include <vector>
#include <format>
#include <cstddef>
#include <type_traits>
#include <expected>
#include <exception>
#include <utility>

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
};

struct ValidationError {
    std::string path;
    std::string message;
    std::string annotation;
};

enum class Mode { CollectAll, FailFast };

struct ValidationContext {
    std::vector<ValidationError> errors;
    std::vector<std::string> path_stack;
    Mode mode = Mode::CollectAll;

    bool should_stop() const {
        return mode == Mode::FailFast && !errors.empty();
    }

    std::string current_path() const {
        std::string result;
        for (std::size_t i = 0; i < path_stack.size(); ++i) {
            if (i > 0) result += '.';
            result += path_stack[i];
        }
        return result;
    }
};

class ValidationException : public std::exception {
public:
    std::vector<ValidationError> errors;

    explicit ValidationException(std::vector<ValidationError> e)
        : errors(std::move(e)),
          message_(std::format("validation failed with {} error(s)", errors.size())) {}

    const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string message_;
};

namespace detail {

template <typename T>
void validate_impl(const T& obj, ValidationContext& ctx) {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked())))
    {
        if (!ctx.should_stop()) {
            ctx.path_stack.push_back(
                std::string{std::meta::identifier_of(member)});

            template for (constexpr auto ann :
                          std::define_static_array(
                              std::meta::annotations_of(member)))
            {
                if (!ctx.should_stop()) {
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
            }

            using MT = std::remove_cvref_t<decltype(obj.[:member:])>;
            if constexpr (std::is_aggregate_v<MT>) {
                if (!ctx.should_stop()) {
                    validate_impl(obj.[:member:], ctx);
                }
            }

            ctx.path_stack.pop_back();
        }
    }
}

}  // namespace detail

template <typename T>
std::vector<ValidationError> collect(const T& obj, Mode mode = Mode::CollectAll) {
    ValidationContext ctx;
    ctx.mode = mode;
    detail::validate_impl(obj, ctx);
    return std::move(ctx.errors);
}

template <typename T>
std::expected<void, std::vector<ValidationError>>
check(const T& obj, Mode mode = Mode::CollectAll) {
    auto errs = collect(obj, mode);
    if (errs.empty()) return {};
    return std::unexpected{std::move(errs)};
}

template <typename T>
void validate(const T& obj, Mode mode = Mode::CollectAll) {
    auto errs = collect(obj, mode);
    if (!errs.empty()) throw ValidationException{std::move(errs)};
}

std::string format_error(const ValidationError& e) {
    return std::format("{}: {} ({})", e.path, e.message, e.annotation);
}

int main() {
    User good{30, "alice", "a@b.c", {"123 Main St", 12345}};
    User multi_fail{200, "al", "", {"X", 0}};

    auto print_errs = [](const std::vector<ValidationError>& errs) {
        for (const auto& e : errs) std::cout << "  " << format_error(e) << '\n';
    };

    // --- collect (vector) ---
    std::cout << "=== collect(multi_fail) ===\n";
    auto errs = collect(multi_fail);
    std::cout << "errors=" << errs.size() << '\n';
    print_errs(errs);

    // --- check (expected) ---
    std::cout << "\n=== check(good) ===\n";
    auto r_good = check(good);
    std::cout << std::boolalpha
              << "has_value=" << r_good.has_value() << '\n';

    std::cout << "\n=== check(multi_fail) ===\n";
    auto r_bad = check(multi_fail);
    std::cout << "has_value=" << r_bad.has_value()
              << ", errors=" << r_bad.error().size() << '\n';
    print_errs(r_bad.error());

    // --- validate (throws) ---
    std::cout << "\n=== validate(good) ===\n";
    try {
        validate(good);
        std::cout << "passed (no throw)\n";
    } catch (const ValidationException& ex) {
        std::cout << "threw: " << ex.what() << '\n';
    }

    std::cout << "\n=== validate(multi_fail) ===\n";
    try {
        validate(multi_fail);
        std::cout << "passed (no throw)\n";
    } catch (const ValidationException& ex) {
        std::cout << "threw: " << ex.what()
                  << " (errors=" << ex.errors.size() << ")\n";
        print_errs(ex.errors);
    }

    // --- Mode::FailFast ---
    std::cout << "\n=== collect(multi_fail, FailFast) ===\n";
    auto ff_errs = collect(multi_fail, Mode::FailFast);
    std::cout << "errors=" << ff_errs.size() << '\n';
    print_errs(ff_errs);

    return 0;
}
