// Stage 1: Hello reflection.
// Annotation 없이, struct의 nonstatic data member 이름만 출력.
// 환경과 리플렉션 파이프라인이 살아있는지 확인하는 목적.

#include <meta>
#include <iostream>
#include <string>

struct User {
    std::string name;
    int age;
    std::string email;
};

int main() {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^User, std::meta::access_context::unchecked())))
    {
        std::cout << std::meta::identifier_of(member) << '\n';
    }
    return 0;
}
