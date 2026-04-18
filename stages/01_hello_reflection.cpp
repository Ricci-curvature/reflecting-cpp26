// Stage 1: Hello reflection.
// No annotations yet — just print the nonstatic data member names of a struct.
// Purpose: sanity-check that the toolchain and reflection pipeline are alive.

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
