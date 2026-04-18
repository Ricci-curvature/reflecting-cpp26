// Verbatim README first-example, kept as a compiled check that the output
// block in README.md stays accurate. Not a unit test — just a compile/run
// sanity check of the landing example.

#include <validator.hpp>
#include <iostream>

struct Address {
    [[=av::MinLength{2}]]    std::string street;
    [[=av::Range{1, 99999}]] int         zip_code;
};

struct User {
    [[=av::Range{0, 150}]] int         age;
    [[=av::MinLength{3}]]  std::string name;
    Address                            address;
};

int main() {
    User u{200, "al", {"X", 0}};
    auto errors = av::collect(u);
    for (const auto& e : errors) std::cout << av::format_error(e) << '\n';
}
