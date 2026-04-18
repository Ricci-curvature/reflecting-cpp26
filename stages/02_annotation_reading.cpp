// Stage 2: Reading annotations.
// Verify that both annotation shapes (literal class with members / empty tag
// type) flow through the same pipeline, and that a field with no annotations
// is silently skipped via the empty annotations_of range.

#include <meta>
#include <iostream>
#include <string>

struct Range {
    long long min, max;
    constexpr Range(long long lo, long long hi) : min(lo), max(hi) {}
};

struct NotEmpty {};

struct Sample {
    [[=Range{1, 100}]] int count;
    [[=NotEmpty{}]] std::string name;
    int no_annotation;
};

int main() {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^Sample, std::meta::access_context::unchecked())))
    {
        std::cout << "field: " << std::meta::identifier_of(member) << '\n';

        constexpr auto anns = std::define_static_array(
            std::meta::annotations_of(member));

        if constexpr (anns.size() == 0) {
            std::cout << "  (no annotations)\n";
        } else {
            template for (constexpr auto ann : anns) {
                if constexpr (std::meta::type_of(ann) == ^^Range) {
                    constexpr auto r = std::meta::extract<Range>(ann);
                    std::cout << "  annotation: Range { min=" << r.min
                              << ", max=" << r.max << " }\n";
                } else if constexpr (std::meta::type_of(ann) == ^^NotEmpty) {
                    std::cout << "  annotation: NotEmpty\n";
                }
            }
        }
    }
    return 0;
}
