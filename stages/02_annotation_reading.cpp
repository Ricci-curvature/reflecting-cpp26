// Stage 2: Annotation 읽기.
// 두 가지 모양(멤버 있는 literal class / 빈 태그 타입)의 annotation이
// 같은 파이프라인으로 처리되는지, 그리고 annotation 없는 필드가
// 빈 range로 스킵되는지 확인.

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
