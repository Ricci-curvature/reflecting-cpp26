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
//   - Compile-time     : passes(obj)         → constexpr bool
//                        first_error(obj)    → constexpr std::string, "" if valid
//                        Meant for `static_assert(passes(obj), first_error(obj))`.
//                        Annotation messages are built with std::to_chars (C++23
//                        constexpr per P2291) so they survive the compile-time
//                        path; byte-identical to std::format's runtime output.
//   - JSON Schema      : json_schema<T>()    → constexpr std::string
//                        Projects a struct's reflected members + annotations
//                        into a JSON Schema document. Uses the same
//                        annotation-is-the-protocol shape as `validate()`:
//                        each annotation owns a
//                          template<typename V>
//                          constexpr void schema_emit(SchemaContext&) const;
//                        that pushes per-member fragments (e.g. `"minimum":0`)
//                        or flips the `required` flag (NotNullopt). The walker
//                        never dispatches on annotation identity; it asks
//                        `requires { a.template schema_emit<V>(sc); }` and calls
//                        through, mirroring Stage 18's validate() protocol.
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
#include <charconv>
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

// Mini integral-to-string formatter.
//
// std::format is not yet constexpr in this libc++ build, so annotation
// messages and path rendering use std::to_chars (constexpr since C++23,
// P2291) + string concat. Output is byte-identical to the std::format
// equivalents these calls replace.
constexpr std::string to_str(long long x) {
    char buf[24];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), x);
    return std::string(buf, ptr);
}

constexpr std::string to_str(std::size_t x) {
    char buf[24];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), x);
    return std::string(buf, ptr);
}

// SchemaContext: the parallel of ValidationContext for JSON Schema
// emission. Annotations push JSON key/value fragments (bodies only,
// e.g. `"minimum":0`) into `fragments`, and flip `required` if they
// mean "this member must be present" (NotNullopt). The outer emitter
// wraps fragments in `{...}` for the member and lifts `required`
// upward into the parent object's `"required":[...]` list.
struct SchemaContext {
    std::vector<std::string> fragments;
    bool required = false;
};

using PathSegment = std::variant<std::string, std::size_t>;

struct ValidationContext {
    std::vector<ValidationError> errors;
    std::vector<PathSegment> path_stack;
    Mode mode = Mode::CollectAll;

    constexpr bool should_stop() const {
        return mode == Mode::FailFast && !errors.empty();
    }

    constexpr std::string current_path() const {
        std::string r;
        for (const auto& seg : path_stack) {
            if (std::holds_alternative<std::size_t>(seg)) {
                r += '[';
                r += to_str(std::get<std::size_t>(seg));
                r += ']';
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
constexpr void walk_members(const T& obj, ValidationContext& ctx);

template <std::meta::info Member, typename V>
constexpr void dispatch_value(const V& v, ValidationContext& ctx) {
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
constexpr void walk_members(const T& obj, ValidationContext& ctx) {
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
                std::string msg;
                msg += "must be in [";
                msg += detail::to_str(min);
                msg += ", ";
                msg += detail::to_str(max);
                msg += "], got ";
                msg += detail::to_str(static_cast<long long>(v));
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::move(msg),
                    "Range"
                });
            }
        }
    }

    // Schema: arithmetic field → "minimum":lo,"maximum":hi.
    // bool is excluded (JSON Schema bool has no numeric bounds).
    template <typename V>
    constexpr void schema_emit(detail::SchemaContext& sc) const {
        if constexpr (std::is_arithmetic_v<V> && !std::is_same_v<V, bool>) {
            std::string mn;
            mn += "\"minimum\":";
            mn += detail::to_str(min);
            sc.fragments.push_back(std::move(mn));
            std::string mx;
            mx += "\"maximum\":";
            mx += detail::to_str(max);
            sc.fragments.push_back(std::move(mx));
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
                std::string msg;
                msg += "length must be >= ";
                msg += detail::to_str(value);
                msg += ", got ";
                msg += detail::to_str(v.size());
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::move(msg),
                    "MinLength"
                });
            }
        }
    }

    // Schema: only meaningful for string — "minLength":N.
    template <typename V>
    constexpr void schema_emit(detail::SchemaContext& sc) const {
        if constexpr (std::is_same_v<V, std::string>) {
            std::string f;
            f += "\"minLength\":";
            f += detail::to_str(value);
            sc.fragments.push_back(std::move(f));
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
                std::string msg;
                msg += "length must be <= ";
                msg += detail::to_str(value);
                msg += ", got ";
                msg += detail::to_str(v.size());
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::move(msg),
                    "MaxLength"
                });
            }
        }
    }

    // Schema: only meaningful for string — "maxLength":N.
    template <typename V>
    constexpr void schema_emit(detail::SchemaContext& sc) const {
        if constexpr (std::is_same_v<V, std::string>) {
            std::string f;
            f += "\"maxLength\":";
            f += detail::to_str(value);
            sc.fragments.push_back(std::move(f));
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

    // Schema: string → "minLength":1; vector → "minItems":1.
    template <typename V>
    constexpr void schema_emit(detail::SchemaContext& sc) const {
        if constexpr (std::is_same_v<V, std::string>) {
            sc.fragments.push_back(std::string{"\"minLength\":1"});
        } else if constexpr (detail::is_vector_v<V>) {
            sc.fragments.push_back(std::string{"\"minItems\":1"});
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
                std::string msg;
                msg += "size must be >= ";
                msg += detail::to_str(value);
                msg += ", got ";
                msg += detail::to_str(v.size());
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::move(msg),
                    "MinSize"
                });
            }
        }
    }

    // Schema: vector → "minItems":N.
    template <typename V>
    constexpr void schema_emit(detail::SchemaContext& sc) const {
        if constexpr (detail::is_vector_v<V>) {
            std::string f;
            f += "\"minItems\":";
            f += detail::to_str(value);
            sc.fragments.push_back(std::move(f));
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
                std::string msg;
                msg += "size must be <= ";
                msg += detail::to_str(value);
                msg += ", got ";
                msg += detail::to_str(v.size());
                ctx.errors.push_back({
                    ctx.current_path(),
                    std::move(msg),
                    "MaxSize"
                });
            }
        }
    }

    // Schema: vector → "maxItems":N.
    template <typename V>
    constexpr void schema_emit(detail::SchemaContext& sc) const {
        if constexpr (detail::is_vector_v<V>) {
            std::string f;
            f += "\"maxItems\":";
            f += detail::to_str(value);
            sc.fragments.push_back(std::move(f));
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

    // Schema: cross-cutting — flips the member's `required` flag, which
    // the parent's emitter lifts into its `"required":[...]` list.
    // Contributes nothing to the member's own fragment.
    template <typename V>
    constexpr void schema_emit(detail::SchemaContext& sc) const {
        sc.required = true;
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

    // Schema: an arbitrary callable isn't expressible in JSON Schema, so
    // we emit "$comment":"predicate: <message>" as a honest marker. Tools
    // that consume this schema see the constraint exists without being
    // able to enforce it.
    template <typename V>
    constexpr void schema_emit(detail::SchemaContext& sc) const {
        std::string f;
        f += "\"$comment\":\"predicate: ";
        f += std::string{message};
        f += '"';
        sc.fragments.push_back(std::move(f));
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

// ---- Compile-time entry points ----
//
// passes(obj)      → true iff no annotation rejected any member.
// first_error(obj) → "<path>: <message> (<annotation>)" for the first
//                    collected error, empty string if none.
//
// Intended use:
//     static_assert(av::passes(obj), av::first_error(obj));
//
// Both are also callable at runtime. Messages are byte-identical to the
// runtime walker's — see detail::to_str above for how that holds without
// std::format.
//
// Constraint to be aware of: namespace-scope `constexpr T obj{...};` works
// only for types whose members have no transient heap past their
// initializer (e.g. short SSO strings). For types containing
// `std::vector`, construct inside a `consteval` helper and call
// passes/first_error there.

template <typename T>
constexpr bool passes(const T& obj) {
    detail::ValidationContext ctx;
    detail::walk_members(obj, ctx);
    return ctx.errors.empty();
}

template <typename T>
constexpr std::string first_error(const T& obj) {
    detail::ValidationContext ctx;
    detail::walk_members(obj, ctx);
    if (ctx.errors.empty()) return {};
    const auto& e = ctx.errors.front();
    std::string r;
    r += e.path;
    r += ": ";
    r += e.message;
    r += " (";
    r += e.annotation;
    r += ")";
    return r;
}

// ---- JSON Schema export ----
//
// `json_schema<T>()` walks the same reflection surface as `passes` /
// `first_error`, but invokes each annotation's `schema_emit<V>()`
// instead of `validate(v, ctx)`. Same members, same annotations,
// different projection.
//
// The output is a JSON Schema document. `{"type":"object","properties":{...}}`
// for aggregates; nested aggregates recurse; `std::optional<U>` unwraps
// to U; `std::vector<U>` emits `"type":"array","items":<U's schema>`.
// NotNullopt is structural — it flips the member's required flag,
// which the parent emitter lifts into its `"required":[...]`.
//
// No whitespace, no `"$schema"` header — just the minimal shape needed
// to pin with `static_assert(json_schema<T>() == R"(...)")`.
// Callers that need pretty-printing or draft URIs can wrap the output.

namespace detail {

// effective_t<V>: peel one layer of std::optional. JSON Schema has no
// "nullable" in draft-07 core, so optional<U> is emitted as U and the
// member's presence is governed by whether NotNullopt was attached.
template <typename V>
struct effective { using type = V; };
template <typename U>
struct effective<std::optional<U>> { using type = U; };
template <typename V>
using effective_t = typename effective<V>::type;

// emit_type_keyword<V>: the leading `"type":"..."` for a value type.
// Returns empty string for aggregate/unknown (caller decides the shape).
template <typename V>
constexpr std::string emit_type_keyword() {
    if constexpr (std::is_same_v<V, bool>) {
        return std::string{"\"type\":\"boolean\""};
    } else if constexpr (std::is_integral_v<V>) {
        return std::string{"\"type\":\"integer\""};
    } else if constexpr (std::is_floating_point_v<V>) {
        return std::string{"\"type\":\"number\""};
    } else if constexpr (std::is_same_v<V, std::string>) {
        return std::string{"\"type\":\"string\""};
    } else if constexpr (is_vector_v<V>) {
        return std::string{"\"type\":\"array\""};
    } else {
        return std::string{};
    }
}

template <typename T>
constexpr std::string emit_object_schema();

// emit_leaf_schema<V>: for a non-aggregate value type (with no
// annotations available), produce `{"type":"..."}`. Used for
// vector `items` where the element type doesn't carry annotations.
template <typename V>
constexpr std::string emit_leaf_schema() {
    if constexpr (std::is_aggregate_v<V> && !std::is_same_v<V, std::string>
                  && !is_vector_v<V> && !is_optional_v<V>) {
        return emit_object_schema<V>();
    } else if constexpr (is_vector_v<V>) {
        std::string r;
        r += "{\"type\":\"array\",\"items\":";
        r += emit_leaf_schema<typename V::value_type>();
        r += '}';
        return r;
    } else if constexpr (is_optional_v<V>) {
        return emit_leaf_schema<typename V::value_type>();
    } else {
        std::string r;
        r += '{';
        r += emit_type_keyword<V>();
        r += '}';
        return r;
    }
}

// emit_member_schema<Member, V>: emit `{...}` for one data member and
// return the `required` flag for the parent to lift. Handles
// optional unwrap, nested aggregates (recurses into emit_object_schema),
// vectors (emits `"items"`), and overlays per-annotation fragments.
template <std::meta::info Member, typename V>
constexpr std::pair<std::string, bool> emit_member_schema() {
    using E = effective_t<V>;

    // Collect annotation contributions.
    SchemaContext sc;
    template for (constexpr auto ann :
                  std::define_static_array(
                      std::meta::annotations_of(Member)))
    {
        using A = [:std::meta::type_of(ann):];
        constexpr auto a = std::meta::extract<A>(ann);
        if constexpr (requires { a.template schema_emit<E>(sc); }) {
            a.template schema_emit<E>(sc);
        }
    }

    // Nested aggregate member: delegate to emit_object_schema, then
    // annotations are only consulted for the `required` flag (object-level
    // annotation keywords are out of scope for this stage).
    if constexpr (std::is_aggregate_v<E>
                  && !std::is_same_v<E, std::string>
                  && !is_vector_v<E>
                  && !is_optional_v<E>) {
        return {emit_object_schema<E>(), sc.required};
    }

    // Scalar / string / vector: compose `{"type":"...", <frags>, <items>?}`.
    std::string r;
    r += '{';
    const std::string type_kw = emit_type_keyword<E>();
    bool need_comma = false;
    if (!type_kw.empty()) {
        r += type_kw;
        need_comma = true;
    }
    for (const auto& frag : sc.fragments) {
        if (need_comma) r += ',';
        r += frag;
        need_comma = true;
    }
    if constexpr (is_vector_v<E>) {
        if (need_comma) r += ',';
        r += "\"items\":";
        r += emit_leaf_schema<typename E::value_type>();
    }
    r += '}';
    return {r, sc.required};
}

template <typename T>
constexpr std::string emit_object_schema() {
    std::string props;
    std::string required_list;
    bool props_first = true;
    bool req_first = true;

    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked())))
    {
        using V = [:std::meta::type_of(member):];
        constexpr auto name_view = std::meta::identifier_of(member);

        auto [member_schema, required] = emit_member_schema<member, V>();

        if (!props_first) props += ',';
        props += '"';
        props += std::string{name_view};
        props += "\":";
        props += member_schema;
        props_first = false;

        if (required) {
            if (!req_first) required_list += ',';
            required_list += '"';
            required_list += std::string{name_view};
            required_list += '"';
            req_first = false;
        }
    }

    std::string r;
    r += "{\"type\":\"object\",\"properties\":{";
    r += props;
    r += '}';
    if (!required_list.empty()) {
        r += ",\"required\":[";
        r += required_list;
        r += ']';
    }
    r += '}';
    return r;
}

}  // namespace detail

template <typename T>
constexpr std::string json_schema() {
    if constexpr (std::is_aggregate_v<T> && !std::is_same_v<T, std::string>
                  && !detail::is_vector_v<T> && !detail::is_optional_v<T>) {
        return detail::emit_object_schema<T>();
    } else {
        return detail::emit_leaf_schema<T>();
    }
}

}  // namespace av

#endif  // AV_VALIDATOR_HPP
