// C++26 Annotation-Based Validator — header-only release.
// Bloomberg clang-p2996 fork required (-std=c++26 -freflection-latest).
//
// Public API (namespace av):
//   - Annotation types: Range, MinLength, MaxLength, NotEmpty
//   - ValidationError, ValidationContext, ValidationException, Mode
//   - collect(obj, mode)  → std::vector<ValidationError>
//   - check(obj, mode)    → std::expected<void, std::vector<ValidationError>>
//   - validate(obj, mode) → void, throws ValidationException on failure
//   - format_error(err)   → "<path>: <message> (<annotation>)"
//
// ODR: all non-template free functions are inline; class member functions are
// defined in-class (implicitly inline); templates are implicitly inline. Safe
// to include across multiple translation units.

#ifndef AV_VALIDATOR_HPP
#define AV_VALIDATOR_HPP

#include <meta>
#include <string>
#include <vector>
#include <format>
#include <cstddef>
#include <type_traits>
#include <expected>
#include <exception>
#include <utility>

namespace av {

// ---- Annotation types ----

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

// ---- Error + context ----

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

// ---- Core engine ----

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

// ---- Policy layer ----

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

inline std::string format_error(const ValidationError& e) {
    return std::format("{}: {} ({})", e.path, e.message, e.annotation);
}

}  // namespace av

#endif  // AV_VALIDATOR_HPP
