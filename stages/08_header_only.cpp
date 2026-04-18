// Stage 8: Header-only driver.
// Library lives in `include/validator.hpp`. This TU speaks only the public API
// (namespace `av`) and proves the header compiles standalone with no stray
// `using`s, no `<iostream>` leakage from the library, and no redundant copies
// of Stage 7's types carried over.

#include "../include/validator.hpp"

#include <iostream>
#include <string>
#include <vector>

// Defined in stages/08_aux_tu.cpp — second TU that also includes the header.
// If you see "multiple definition of av::..." from the linker, something in the
// header lost its inline/template/namespace-local guarantee.
std::size_t count_errors_in_aux_tu();

struct Address {
    [[=av::MinLength{2}]] std::string street;
    [[=av::Range{1, 99999}]] int zip_code;
};

struct User {
    [[=av::Range{0, 150}]] int age;
    [[=av::MinLength{3}]] [[=av::MaxLength{32}]] std::string name;
    [[=av::NotEmpty{}]] std::string email;
    Address address;
};

int main() {
    User good{30, "alice", "a@b.c", {"123 Main St", 12345}};
    User multi_fail{200, "al", "", {"X", 0}};

    auto print_errs = [](const std::vector<av::ValidationError>& errs) {
        for (const auto& e : errs) std::cout << "  " << av::format_error(e) << '\n';
    };

    std::cout << "=== collect(multi_fail) ===\n";
    auto errs = av::collect(multi_fail);
    std::cout << "errors=" << errs.size() << '\n';
    print_errs(errs);

    std::cout << "\n=== check(good) ===\n";
    auto r_good = av::check(good);
    std::cout << std::boolalpha
              << "has_value=" << r_good.has_value() << '\n';

    std::cout << "\n=== check(multi_fail) ===\n";
    auto r_bad = av::check(multi_fail);
    std::cout << "has_value=" << r_bad.has_value()
              << ", errors=" << r_bad.error().size() << '\n';
    print_errs(r_bad.error());

    std::cout << "\n=== validate(good) ===\n";
    try {
        av::validate(good);
        std::cout << "passed (no throw)\n";
    } catch (const av::ValidationException& ex) {
        std::cout << "threw: " << ex.what() << '\n';
    }

    std::cout << "\n=== validate(multi_fail) ===\n";
    try {
        av::validate(multi_fail);
        std::cout << "passed (no throw)\n";
    } catch (const av::ValidationException& ex) {
        std::cout << "threw: " << ex.what()
                  << " (errors=" << ex.errors.size() << ")\n";
        print_errs(ex.errors);
    }

    std::cout << "\n=== collect(multi_fail, FailFast) ===\n";
    auto ff_errs = av::collect(multi_fail, av::Mode::FailFast);
    std::cout << "errors=" << ff_errs.size() << '\n';
    print_errs(ff_errs);

    std::cout << "\n=== aux TU (ODR smoke test) ===\n";
    std::cout << "aux_tu_errors=" << count_errors_in_aux_tu() << '\n';

    return 0;
}
