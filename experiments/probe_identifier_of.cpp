// Probe: std::meta::identifier_of on various annotation type shapes.
//
// For the protocol-based Stage 18, we'd like dispatch to auto-extract the
// annotation name so validate() bodies don't hardcode "Range" / "Predicate"
// etc. This probe answers:
//
//   - Does identifier_of(^^SimpleStruct) give "SimpleStruct"?
//   - Does identifier_of(^^TemplateClass<Args>) give "TemplateClass"
//     (primary template name) or something with args?
//   - What about closure types as the Args?

#include <meta>
#include <iostream>
#include <string>
#include <string_view>

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}
};

template <typename F>
struct Predicate { F f; };

struct Probe {
    [[=Range{0, 150}]]                          int a;
    [[=Predicate{[](int x) { return x > 0; }}]] int b;
};

int main() {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^Probe, std::meta::access_context::unchecked())))
    {
        std::cout << "field: "
                  << std::meta::identifier_of(member) << '\n';

        template for (constexpr auto ann :
                      std::define_static_array(
                          std::meta::annotations_of(member)))
        {
            constexpr auto t = std::meta::type_of(ann);
            // Attempt 1: identifier_of directly on the instantiated type.
            if constexpr (requires { std::meta::identifier_of(t); }) {
                std::cout << "  direct identifier_of(type_of): "
                          << std::meta::identifier_of(t) << '\n';
            } else {
                std::cout << "  direct identifier_of(type_of): "
                          << "(rejected by requires)\n";
            }

            // Attempt 2: if it's a template specialization, peel the
            // primary template with template_of and ask identifier of that.
            if constexpr (requires { std::meta::template_of(t); }) {
                constexpr auto primary = std::meta::template_of(t);
                std::cout << "  template_of present: "
                          << std::meta::identifier_of(primary) << '\n';
            } else {
                std::cout << "  template_of present: (no)\n";
            }
        }
    }
}
