// Q1 + Q3 probe for Stage 18. Two concerns:
//
//   Q1. requires { a.validate(v, ctx); } must pass typecheck with a
//       mutable ValidationContext& — the body pushes errors, so ctx is
//       non-const. constexpr-context has nothing to do with it (walker
//       runs at runtime), but confirm.
//
//   Q3. Stage 5 observed: template-for with reflection-dependent
//       if constexpr branches can fail to discard on non-matching members.
//       Here we mix protocol-style annotations (have .validate) with a
//       legacy value-carrier (Range-like struct with no validate member)
//       on different fields, and see if the dispatch ladder handles both
//       without typechecking the wrong body against the wrong value.

#include <meta>
#include <iostream>
#include <string>
#include <vector>
#include <format>

struct ValidationContext {
    std::vector<std::string> errors;
};

// Protocol-style: has validate(v, ctx).
struct StartsWithUppercase {
    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if (v.empty() || v[0] < 'A' || v[0] > 'Z') {
            ctx.errors.push_back("must start with uppercase");
        }
    }
};

// Protocol-style with a different shape: operates on int.
struct MustBePositive {
    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if (v <= 0) {
            ctx.errors.push_back(std::format("must be positive, got {}", v));
        }
    }
};

// Legacy-style: plain value carrier, no validate member.
struct RangeLegacy {
    long long min, max;
    constexpr RangeLegacy(long long lo, long long hi) : min(lo), max(hi) {}
};

struct Probe {
    [[=StartsWithUppercase{}]] std::string name;
    [[=MustBePositive{}]]      int         count;
    [[=RangeLegacy{0, 150}]]   int         age;
};

int main() {
    Probe p{.name = "alice", .count = -3, .age = 200};
    ValidationContext ctx;

    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^Probe, std::meta::access_context::unchecked())))
    {
        template for (constexpr auto ann :
                      std::define_static_array(
                          std::meta::annotations_of(member)))
        {
            using A = [:std::meta::type_of(ann):];
            constexpr auto a = std::meta::extract<A>(ann);

            // Protocol branch: has validate(v, ctx).
            if constexpr (requires { a.validate(p.[:member:], ctx); }) {
                a.validate(p.[:member:], ctx);
            }
            // Legacy branch: falls back to explicit dispatch. This is
            // what Stage 18 will use to keep existing annotations
            // working while the protocol is introduced.
            else if constexpr (std::meta::type_of(ann) == ^^RangeLegacy) {
                if constexpr (requires { p.[:member:] < 0LL; }) {
                    if (p.[:member:] < a.min || p.[:member:] > a.max) {
                        ctx.errors.push_back(std::format(
                            "out of range [{}, {}], got {}",
                            a.min, a.max,
                            static_cast<long long>(p.[:member:])));
                    }
                }
            }
        }
    }

    for (const auto& e : ctx.errors) {
        std::cout << e << '\n';
    }
}
