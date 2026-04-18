// Step B smoke: verify std::meta::nonstatic_data_members_of + template for
// + identifier_of round-trip with libc++.
// API shape confirmed from clang-p2996 test
//   libcxx/test/std/experimental/reflection/members-and-subobjects.pass.cpp

#include <meta>
#include <iostream>
#include <string_view>

struct Point {
    int x;
    int y;
};

int main() {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^Point, std::meta::access_context::unchecked())))
    {
        std::cout << std::meta::identifier_of(member) << '\n';
    }
    return 0;
}
