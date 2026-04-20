// C++26 Annotation-Based Validator — header-only release.
// Bloomberg clang-p2996 fork required (-std=c++26 -freflection-latest).
//
// Public API (namespace av):
//   - Annotation types : Range, MinLength, MaxLength, NotEmpty,
//                        MinSize, MaxSize, NotNullopt, Predicate<F, N>
//                        Each is a protocol-based validator: it owns a
//                        constexpr template `validate(const V&, Ctx&)`
//                        member that the walker invokes through a
//                        `requires { a.validate(v, ctx); }` guard. Users
//                        can define their own annotations by writing a
//                        struct with the same shape and attaching it
//                        with `[[=UserAnnotation{}]]`.
//   - Error types      : ValidationError, ValidationException
//   - Mode enum        : Mode::CollectAll, Mode::FailFast
//   - Policy functions : collect(obj, mode)  → std::vector<ValidationError>
//                        check(obj, mode)    → std::expected<void, vector<ValidationError>>
//                        validate(obj, mode) → void, throws ValidationException on failure
//   - Helper           : format_error(err)   → "<path>: <message> (<annotation>)"
//
// Internal (namespace av::detail, not stable, not user-facing):
//   - is_optional_v, is_vector_v, PathSegment, ValidationContext,
//     walk_members<T>, dispatch_value<Member, V>
//
// Path rendering: field chains render dotted (`user.address.street`),
// container indices render as `[N]` (`emails[0].value`), with no
// leading dot before the first segment.
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
#include <optional>
#include <variant>
#include <concepts>

namespace av {

// ---- Error, mode, exception (public) ----

struct ValidationError {
    std::string path;
    std::string message;
    std::string annotation;
};

enum class Mode { CollectAll, FailFast };

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

// ---- Internal: traits, context, walker ----

namespace detail {

template <typename T>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};
template <typename T>
constexpr bool is_optional_v = is_optional<T>::value;

template <typename T>
struct is_vector : std::false_type {};
template <typename T>
struct is_vector<std::vector<T>> : std::true_type {};
template <typename T>
constexpr bool is_vector_v = is_vector<T>::value;

using PathSegment = std::variant<std::string, std::size_t>;

struct ValidationContext {
    std::vector<ValidationError> errors;
    std::vector<PathSegment> path_stack;
    Mode mode = Mode::CollectAll;

    bool should_stop() const {
        return mode == Mode::FailFast && !errors.empty();
    }

    std::string current_path() const {
        std::string r;
        for (const auto& seg : path_stack) {
            if (std::holds_alternative<std::size_t>(seg)) {
                r += std::format("[{}]", std::get<std::size_t>(seg));
            } else {
                const auto& name = std::get<std::string>(seg);
                if (!r.empty()) r += '.';
                r += name;
            }
        }
        return r;
    }
};

template <typename T>
void walk_members(const T& obj, ValidationContext& ctx);

template <std::meta::info Member, typename V>
void dispatch_value(const V& v, ValidationContext& ctx) {
    template for (constexpr auto ann :
                  std::define_static_array(
                      std::meta::annotations_of(Member)))
    {
        if (!ctx.should_stop()) {
            using A = [:std::meta::type_of(ann):];
            constexpr auto a = std::meta::extract<A>(ann);
            if constexpr (requires { a.validate(v, ctx); }) {
                a.validate(v, ctx);
            }
        }
    }

    if constexpr (is_optional_v<V>) {
        if (v.has_value() && !ctx.should_stop()) {
            dispatch_value<Member>(*v, ctx);
        }
    } else if constexpr (is_vector_v<V>) {
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (ctx.should_stop()) break;
            ctx.path_stack.push_back(i);
            dispatch_value<Member>(v[i], ctx);
            ctx.path_stack.pop_back();
        }
    } else if constexpr (std::is_aggregate_v<V>) {
        if (!ctx.should_stop()) {
            walk_members(v, ctx);
        }
    }
}

template <typename T>
void walk_members(const T& obj, ValidationContext& ctx) {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked())))
    {
        if (!ctx.should_stop()) {
            ctx.path_stack.push_back(
                std::string{std::meta::identifier_of(member)});
            dispatch_value<member>(obj.[:member:], ctx);
            ctx.path_stack.pop_back();
        }
    }
}

}  // namespace detail

// ---- Annotations (all protocol-based) ----
//
// Each annotation owns two things:
//   1. Its payload (min/max, length bound, predicate callable, ...).
//   2. A constexpr template validate(const V&, Ctx&) member that
//      decides its own apply-site via requires + is_optional_v /
//      is_vector_v guards, and pushes ValidationError on failure.
//
// The walker's dispatch has no knowledge of any specific annotation —
// it just asks "can you validate?" and calls through.

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { v < 0LL; }
                   && !detail::is_optional_v<V>
                   && !detail::is_vector_v<V>) {
            if (v < min || v > max) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::format("must be in [{}, {}], got {}", min, max, v),
                    "Range"
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
        if constexpr (requires { v.size(); }
                   && !detail::is_optional_v<V>
                   && !detail::is_vector_v<V>) {
            if (v.size() < value) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::format("length must be >= {}, got {}", value, v.size()),
                    "MinLength"
                });
            }
        }
    }
};

struct MaxLength {
    std::size_t value;
    constexpr MaxLength(std::size_t v) : value(v) {}

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { v.size(); }
                   && !detail::is_optional_v<V>
                   && !detail::is_vector_v<V>) {
            if (v.size() > value) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::format("length must be <= {}, got {}", value, v.size()),
                    "MaxLength"
                });
            }
        }
    }
};

struct NotEmpty {
    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { v.empty(); }
                   && !detail::is_optional_v<V>) {
            if (v.empty()) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    "must not be empty",
                    "NotEmpty"
                });
            }
        }
    }
};

struct MinSize {
    std::size_t value;
    constexpr MinSize(std::size_t v) : value(v) {}

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (detail::is_vector_v<V>) {
            if (v.size() < value) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::format("size must be >= {}, got {}", value, v.size()),
                    "MinSize"
                });
            }
        }
    }
};

struct MaxSize {
    std::size_t value;
    constexpr MaxSize(std::size_t v) : value(v) {}

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (detail::is_vector_v<V>) {
            if (v.size() > value) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::format("size must be <= {}, got {}", value, v.size()),
                    "MaxSize"
                });
            }
        }
    }
};

struct NotNullopt {
    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (detail::is_optional_v<V>) {
            if (!v.has_value()) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    "must have a value",
                    "NotNullopt"
                });
            }
        }
    }
};

// Predicate<F, N>: callable + N-sized char array for a custom error
// message. Two deduction guides:
//   Predicate{f}            → Predicate<F, 24> with default message
//   Predicate{f, "..."}     → Predicate<F, N>  with the literal's length
template <typename F, std::size_t N = 24>
struct Predicate {
    F f;
    char message[N] = "custom predicate failed";

    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if constexpr (requires { { f(v) } -> std::same_as<bool>; }) {
            if (!f(v)) {
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::string{message},
                    "Predicate"
                });
            }
        }
    }
};

template <typename F>
Predicate(F) -> Predicate<F, 24>;

template <typename F, std::size_t N>
Predicate(F, const char (&)[N]) -> Predicate<F, N>;

// ---- Policy layer ----

template <typename T>
std::vector<ValidationError> collect(const T& obj, Mode mode = Mode::CollectAll) {
    detail::ValidationContext ctx;
    ctx.mode = mode;
    detail::walk_members(obj, ctx);
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
