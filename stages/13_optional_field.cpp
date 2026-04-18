// Stage 13: std::optional<T> support.
//
// The [previous post]'s aggregate dispatch (validate_impl recurses iff
// is_aggregate_v<MT>) has a blind spot. std::optional<T> is not an aggregate,
// so the walker classifies it as a leaf and silently skips the inner value.
// That's wrong when T is itself a validatable struct, or when the intent is
// "must have a value."
//
// This stage adds:
//   - a type trait `is_optional_v<T>`,
//   - a type-driven recursion branch: if the member is std::optional<Inner>
//     and Inner is aggregate, recurse into the unwrapped value when
//     has_value(),
//   - a `NotNullopt{}` annotation whose dispatch reports an error when the
//     optional is empty.
//
// Scope deliberately held small:
//   - std::optional<Aggregate> recurses into the inner struct.
//   - NotNullopt works for any std::optional regardless of Inner.
//   - std::optional<Scalar> with scalar annotations (e.g. Range on
//     std::optional<int>) is NOT handled here — Range's requires-guard
//     ( v < 0LL ) happens to accept std::optional<int> via optional's
//     heterogeneous operator<, which would silently validate nullopt as
//     "less than anything." The right fix is pulling annotation dispatch
//     out into a helper that takes a value reference, but that rewires
//     enough code that it belongs in a later stage.
//
// Path representation is still flat std::vector<std::string> here — optional
// doesn't add any new segment kind. The variant<string, size_t> switch
// happens when std::vector<T> introduces indices in Stage 14.

#include <meta>
#include <iostream>
#include <string>
#include <vector>
#include <format>
#include <cstddef>
#include <optional>
#include <type_traits>

// ---- Annotation types (subset for this stage) ----

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}
};

struct MinLength {
    std::size_t value;
    constexpr MinLength(std::size_t v) : value(v) {}
};

struct NotNullopt {};

// ---- is_optional trait ----

template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
constexpr bool is_optional_v = is_optional<T>::value;

// ---- Error + context ----

struct ValidationError {
    std::string path;
    std::string message;
    std::string annotation;
};

struct ValidationContext {
    std::vector<ValidationError> errors;
    std::vector<std::string> path_stack;

    std::string current_path() const {
        std::string r;
        for (std::size_t i = 0; i < path_stack.size(); ++i) {
            if (i > 0) r += '.';
            r += path_stack[i];
        }
        return r;
    }
};

// ---- Validator ----

template <typename T>
void validate_impl(const T& obj, ValidationContext& ctx) {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked())))
    {
        ctx.path_stack.push_back(
            std::string{std::meta::identifier_of(member)});

        using MT = std::remove_cvref_t<decltype(obj.[:member:])>;

        // Annotation dispatch — same shape as the previous header.
        template for (constexpr auto ann :
                      std::define_static_array(
                          std::meta::annotations_of(member)))
        {
            if constexpr (std::meta::type_of(ann) == ^^Range) {
                // Guard intentionally excludes std::optional<T> — optional
                // has a heterogeneous operator< that would match this
                // requires-check and validate nullopt as "less than min."
                if constexpr (requires { obj.[:member:] < 0LL; }
                           && !is_optional_v<MT>) {
                    constexpr auto r = std::meta::extract<Range>(ann);
                    auto v = obj.[:member:];
                    if (v < r.min || v > r.max) {
                        ctx.errors.push_back({
                            ctx.current_path(),
                            std::format("must be in [{}, {}], got {}",
                                        r.min, r.max, v),
                            "Range"
                        });
                    }
                }
            } else if constexpr (std::meta::type_of(ann) == ^^MinLength) {
                if constexpr (requires { obj.[:member:].size(); }
                           && !is_optional_v<MT>) {
                    constexpr auto r = std::meta::extract<MinLength>(ann);
                    const auto& v = obj.[:member:];
                    if (v.size() < r.value) {
                        ctx.errors.push_back({
                            ctx.current_path(),
                            std::format("length must be >= {}, got {}",
                                        r.value, v.size()),
                            "MinLength"
                        });
                    }
                }
            } else if constexpr (std::meta::type_of(ann) == ^^NotNullopt) {
                if constexpr (requires { obj.[:member:].has_value(); }) {
                    if (!obj.[:member:].has_value()) {
                        ctx.errors.push_back({
                            ctx.current_path(),
                            "must not be nullopt",
                            "NotNullopt"
                        });
                    }
                }
            }
        }

        // Type-driven recursion.
        if constexpr (is_optional_v<MT>) {
            using Inner = typename MT::value_type;
            if constexpr (std::is_aggregate_v<Inner>) {
                if (obj.[:member:].has_value()) {
                    validate_impl(*obj.[:member:], ctx);
                }
            }
            // std::optional<Scalar> inner-value annotation dispatch:
            // deferred to a later stage.
        } else if constexpr (std::is_aggregate_v<MT>) {
            validate_impl(obj.[:member:], ctx);
        }

        ctx.path_stack.pop_back();
    }
}

// ---- Demo ----

struct Address {
    [[=MinLength{2}]]    std::string street;
    [[=Range{1, 99999}]] int         zip_code;
};

struct User {
    [[=Range{0, 150}]]              int                     age;
    [[=NotNullopt{}]]               std::optional<Address>  address;
    std::optional<Address>                                  prev_address;
    [[=NotNullopt{}]]               std::optional<int>      session_id;
};

int main() {
    // Case 1: required address present but invalid, prev_address absent.
    User u1{
        .age = 200,
        .address = Address{"X", 0},
        .prev_address = std::nullopt,
        .session_id = 42
    };
    ValidationContext c1;
    validate_impl(u1, c1);

    std::cout << "---- case 1: invalid inner, optional absent ----\n";
    for (const auto& e : c1.errors) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }

    // Case 2: required fields are nullopt.
    User u2{
        .age = 30,
        .address = std::nullopt,
        .prev_address = std::nullopt,
        .session_id = std::nullopt
    };
    ValidationContext c2;
    validate_impl(u2, c2);

    std::cout << "\n---- case 2: required optional is nullopt ----\n";
    for (const auto& e : c2.errors) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }

    // Case 3: all valid. prev_address present and also valid.
    User u3{
        .age = 30,
        .address = Address{"Main Street", 10001},
        .prev_address = Address{"Old Road", 20002},
        .session_id = 7
    };
    ValidationContext c3;
    validate_impl(u3, c3);

    std::cout << "\n---- case 3: all valid ----\n";
    if (c3.errors.empty()) {
        std::cout << "  (no errors)\n";
    } else {
        for (const auto& e : c3.errors) {
            std::cout << "  " << e.path << ": " << e.message
                      << " (" << e.annotation << ")\n";
        }
    }

    // Case 4: prev_address present and invalid — recursion still fires
    // even without NotNullopt, because the optional has a value.
    User u4{
        .age = 30,
        .address = Address{"OK Street", 10001},
        .prev_address = Address{"", 0},
        .session_id = 7
    };
    ValidationContext c4;
    validate_impl(u4, c4);

    std::cout << "\n---- case 4: optional present, inner invalid ----\n";
    for (const auto& e : c4.errors) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }

    return 0;
}
