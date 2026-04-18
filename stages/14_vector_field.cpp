// Stage 14: std::vector<T> support + path indexing.
//
// The Stage 13 walker still has the same blind spot for std::vector that it
// had for std::optional before this series started — is_aggregate_v<vector<T>>
// is false, so vector members fall through every recursion branch and their
// elements are never inspected.
//
// Adding the vector branch is shaped like the optional one:
//
//   template <typename T>   struct is_vector                       : std::false_type {};
//   template <typename T>   struct is_vector<std::vector<T>>       : std::true_type {};
//
//   if constexpr (is_vector_v<MT>) {
//       using Elem = typename MT::value_type;
//       if constexpr (std::is_aggregate_v<Elem>) {
//           for (std::size_t i = 0; i < vec.size(); ++i) {
//               ctx.path_stack.push_back(i);
//               validate_impl(vec[i], ctx);
//               ctx.path_stack.pop_back();
//           }
//       }
//   }
//
// But that `push_back(i)` is where the path type breaks. Until now
// `path_stack` has been std::vector<std::string> and segments joined with
// '.'. Indices aren't field names: `emails[0].value` doesn't come from
// joining three identifiers with dots. If I keep path_stack as a flat
// string vector I'd have to bake "[0]" into the string at push time,
// which loses the segment's kind. Filter ValidationErrors by path prefix,
// render to JSON Pointer (/emails/0/value), or localise "[0]" to "첫 번째
// 이메일" — all of that needs to know "this segment is an index, that one
// is a field name." The lossy form throws that away.
//
// So path_stack becomes std::vector<std::variant<std::string, std::size_t>>,
// and current_path() renders each segment according to its alternative:
// field name → optional leading dot + name, index → "[N]" with no
// separator. First-segment-no-leading-dot falls out of the "dot only if
// output is non-empty" rule.
//
// Scope, held small on purpose:
//   - std::vector<Aggregate> recurses into each element.
//   - std::vector<Scalar> (vector<int>, vector<string>) is not
//     element-validated here. That requires the same annotation-dispatch
//     helper refactor that std::optional<Scalar> needed, and it belongs
//     in the stage that does the refactor, not as a side-effect of
//     adding vector.
//   - Nested combos (vector<optional<T>>, optional<vector<T>>): deferred.
//     The branches wouldn't compose correctly — recursion into the inner
//     type goes through validate_impl, which assumes "T is aggregate" via
//     nonstatic_data_members_of. Fixing that is a separate exercise.
//   - Container-level annotations (MinSize/MaxSize) land in Stage 15.
//
// MinLength's guard is widened to exclude vectors, same reasoning as the
// Stage 13 optional trap. vector<T> has .size(), so a bare requires
// { .size() } would silently accept vectors and validate them against a
// string-length bound. The meaning of MinLength is string characters, not
// container elements; MinSize is a separate annotation introduced next
// stage.

#include <meta>
#include <iostream>
#include <string>
#include <vector>
#include <format>
#include <cstddef>
#include <optional>
#include <variant>
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

// ---- Type traits ----

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

// ---- Error + context ----

struct ValidationError {
    std::string path;
    std::string message;
    std::string annotation;
};

using PathSegment = std::variant<std::string, std::size_t>;

struct ValidationContext {
    std::vector<ValidationError> errors;
    std::vector<PathSegment> path_stack;

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

        // Annotation dispatch — scalar / string / optional-marker annotations
        // on this field. Container-level annotations (MinSize, MaxSize) are
        // deferred to the next stage.
        template for (constexpr auto ann :
                      std::define_static_array(
                          std::meta::annotations_of(member)))
        {
            if constexpr (std::meta::type_of(ann) == ^^Range) {
                if constexpr (requires { obj.[:member:] < 0LL; }
                           && !is_optional_v<MT>
                           && !is_vector_v<MT>) {
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
                           && !is_optional_v<MT>
                           && !is_vector_v<MT>) {
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
        } else if constexpr (is_vector_v<MT>) {
            using Elem = typename MT::value_type;
            if constexpr (std::is_aggregate_v<Elem>) {
                const auto& vec = obj.[:member:];
                for (std::size_t i = 0; i < vec.size(); ++i) {
                    ctx.path_stack.push_back(i);
                    validate_impl(vec[i], ctx);
                    ctx.path_stack.pop_back();
                }
            }
        } else if constexpr (std::is_aggregate_v<MT>) {
            validate_impl(obj.[:member:], ctx);
        }

        ctx.path_stack.pop_back();
    }
}

// ---- Demo ----

struct EmailEntry {
    [[=MinLength{3}]] std::string value;
};

struct Address {
    [[=MinLength{2}]]    std::string street;
    [[=Range{1, 99999}]] int         zip_code;
};

struct User {
    [[=Range{0, 150}]]  int                      age;
    std::vector<EmailEntry>                      emails;
    std::vector<Address>                         past_addresses;
    [[=NotNullopt{}]]   std::optional<Address>   current_address;
};

int main() {
    // Case 1: several element-level failures across two vectors.
    User u1{
        .age = 30,
        .emails = {
            EmailEntry{"ok@example.com"},
            EmailEntry{"x"},                  // too short
            EmailEntry{"also@example.com"},
            EmailEntry{""}                    // too short
        },
        .past_addresses = {
            Address{"Main Street", 10001},
            Address{"",            0}          // both fields fail
        },
        .current_address = Address{"Current Ave", 77777}
    };
    ValidationContext c1;
    validate_impl(u1, c1);

    std::cout << "---- case 1: element-level failures across vectors ----\n";
    for (const auto& e : c1.errors) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }

    // Case 2: required optional missing while vectors are fine and non-empty.
    User u2{
        .age = 30,
        .emails = { EmailEntry{"ok@example.com"} },
        .past_addresses = { Address{"Elm", 10002} },
        .current_address = std::nullopt
    };
    ValidationContext c2;
    validate_impl(u2, c2);

    std::cout << "\n---- case 2: required optional missing ----\n";
    for (const auto& e : c2.errors) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }

    // Case 3: all valid.
    User u3{
        .age = 30,
        .emails = { EmailEntry{"a@b.co"}, EmailEntry{"c@d.co"} },
        .past_addresses = { Address{"Elm", 10002} },
        .current_address = Address{"Main", 10001}
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

    // Case 4: empty vectors — every element-level check silently passes
    // because there are no elements. Stage 15 (MinSize) will catch this.
    User u4{
        .age = 30,
        .emails = {},
        .past_addresses = {},
        .current_address = Address{"Somewhere", 10001}
    };
    ValidationContext c4;
    validate_impl(u4, c4);

    std::cout << "\n---- case 4: empty vectors (no element-level errors) ----\n";
    if (c4.errors.empty()) {
        std::cout << "  (no errors — Stage 15 will fix this with MinSize)\n";
    } else {
        for (const auto& e : c4.errors) {
            std::cout << "  " << e.path << ": " << e.message
                      << " (" << e.annotation << ")\n";
        }
    }

    return 0;
}
