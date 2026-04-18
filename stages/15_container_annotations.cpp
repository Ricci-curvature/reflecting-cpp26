// Stage 15: Container-level annotations — MinSize, MaxSize.
//
// Stage 14's element recursion handled the "invalid thing inside the
// container" case. The shape it can't express is the container itself:
//
//     emails must have at least one entry
//     tags must be no longer than five
//
// Adding these is a pure annotation-dispatch extension on top of the
// Stage 14 walker. The recursion logic, the path handling, the traits —
// all unchanged. Two annotation types, two if-constexpr branches, each
// gated on is_vector_v<MT> so they can't accidentally misfire on a
// string (which also has .size()) or an optional (which has .has_value()
// but no element count).
//
// Scope, as before:
//   - MinSize / MaxSize apply to std::vector<T>.
//   - MinLength / MaxLength remain string-only.
//   - optional's "must be present" is NotNullopt, not MinSize{1}.
//   - Other container types (std::array, std::map, ...) are not in the
//     dispatch. Adding each would be a one-line extension of the
//     is_vector_v trait family, but the point of this stage is the
//     annotation type, not the container taxonomy.

#include <meta>
#include <iostream>
#include <string>
#include <vector>
#include <format>
#include <cstddef>
#include <optional>
#include <variant>
#include <type_traits>

// ---- Annotation types ----

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}
};

struct MinLength {
    std::size_t value;
    constexpr MinLength(std::size_t v) : value(v) {}
};

struct MinSize {
    std::size_t value;
    constexpr MinSize(std::size_t v) : value(v) {}
};

struct MaxSize {
    std::size_t value;
    constexpr MaxSize(std::size_t v) : value(v) {}
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
            } else if constexpr (std::meta::type_of(ann) == ^^MinSize) {
                if constexpr (is_vector_v<MT>) {
                    constexpr auto r = std::meta::extract<MinSize>(ann);
                    const auto& v = obj.[:member:];
                    if (v.size() < r.value) {
                        ctx.errors.push_back({
                            ctx.current_path(),
                            std::format("size must be >= {}, got {}",
                                        r.value, v.size()),
                            "MinSize"
                        });
                    }
                }
            } else if constexpr (std::meta::type_of(ann) == ^^MaxSize) {
                if constexpr (is_vector_v<MT>) {
                    constexpr auto r = std::meta::extract<MaxSize>(ann);
                    const auto& v = obj.[:member:];
                    if (v.size() > r.value) {
                        ctx.errors.push_back({
                            ctx.current_path(),
                            std::format("size must be <= {}, got {}",
                                        r.value, v.size()),
                            "MaxSize"
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

struct User {
    [[=Range{0, 150}]]           int                       age;
    [[=MinSize{1}, =MaxSize{5}]] std::vector<EmailEntry>   emails;
    [[=MaxSize{3}]]              std::vector<std::string>  tags;
};

int main() {
    // Case 1: emails empty → MinSize, tags long → MaxSize.
    User u1{
        .age = 30,
        .emails = {},
        .tags = {"a", "b", "c", "d"}
    };
    ValidationContext c1;
    validate_impl(u1, c1);

    std::cout << "---- case 1: emails empty, tags too long ----\n";
    for (const auto& e : c1.errors) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }

    // Case 2: emails too many AND some elements invalid.
    User u2{
        .age = 30,
        .emails = {
            EmailEntry{"a@b.co"},
            EmailEntry{"x"},
            EmailEntry{"c@d.co"},
            EmailEntry{"e@f.co"},
            EmailEntry{"g@h.co"},
            EmailEntry{""}
        },
        .tags = {"ok"}
    };
    ValidationContext c2;
    validate_impl(u2, c2);

    std::cout << "\n---- case 2: emails oversized + invalid elements ----\n";
    for (const auto& e : c2.errors) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }

    // Case 3: all valid.
    User u3{
        .age = 30,
        .emails = { EmailEntry{"a@b.co"}, EmailEntry{"c@d.co"} },
        .tags = {"one", "two"}
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

    // Case 4: both MinSize and MaxSize wouldn't apply — check that
    // MinSize on a vector with just enough and MaxSize still separate.
    User u4{
        .age = 30,
        .emails = { EmailEntry{"a@b.co"} },          // size 1, on the MinSize boundary
        .tags = { "x", "y", "z" }                    // size 3, on the MaxSize boundary
    };
    ValidationContext c4;
    validate_impl(u4, c4);

    std::cout << "\n---- case 4: size bounds inclusive ----\n";
    if (c4.errors.empty()) {
        std::cout << "  (no errors — [>= 1] and [<= 3] are inclusive)\n";
    } else {
        for (const auto& e : c4.errors) {
            std::cout << "  " << e.path << ": " << e.message
                      << " (" << e.annotation << ")\n";
        }
    }

    return 0;
}
