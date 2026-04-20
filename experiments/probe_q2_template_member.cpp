// Q2 probe for Stage 18: can an annotation struct with a template member
// function be used as an NTTP value through [[=...]]?
//
// The structural-type rule (C++26) admits literal classes whose bases and
// non-static data members are public and non-mutable; member *functions*
// don't participate in the equality/equivalence definition. So in principle
// a template member function should be transparent to the NTTP rule. This
// probe verifies clang-p2996 agrees.

#include <meta>
#include <iostream>
#include <string>
#include <vector>

struct ValidationContext {
    std::vector<std::string> errors;
};

struct StartsWithUppercase {
    template <typename V, typename Ctx>
    constexpr void validate(const V& v, Ctx& ctx) const {
        if (v.empty() || v[0] < 'A' || v[0] > 'Z') {
            ctx.errors.push_back("must start with uppercase");
        }
    }
};

struct Probe {
    [[=StartsWithUppercase{}]] std::string name;
};

int main() {
    Probe p{.name = "alice"};
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
            if constexpr (requires { a.validate(p.[:member:], ctx); }) {
                a.validate(p.[:member:], ctx);
            }
        }
    }

    for (const auto& e : ctx.errors) {
        std::cout << e << '\n';
    }
}
