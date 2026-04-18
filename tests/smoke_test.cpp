// User-perspective smoke test.
// A single TU that only talks to the public API in <validator.hpp>:
//   - define a nested struct with a mix of annotations
//   - call collect / check / validate
//   - assert the expected outcomes
//
// Compile:
//   clang++ -std=c++26 -freflection-latest -stdlib=libc++ ... \
//     -I include -o tests/smoke_test.out tests/smoke_test.cpp

#include <validator.hpp>

#include <cassert>
#include <iostream>
#include <string>

struct Address {
    [[=av::MinLength{2}]]    std::string street;
    [[=av::Range{1, 99999}]] int         zip_code;
};

struct User {
    [[=av::Range{0, 150}]]                       int         age;
    [[=av::MinLength{3}]] [[=av::MaxLength{32}]] std::string name;
    [[=av::NotEmpty{}]]                          std::string email;
    Address                                                  address;
};

int main() {
    const User good{30,  "alice", "a@b.c", {"123 Main St", 12345}};
    const User bad { 200, "al",    "",     {"X",           0}};

    // --- collect ---
    assert(av::collect(good).empty());

    const auto bad_errors = av::collect(bad);
    assert(bad_errors.size() == 5);
    assert(bad_errors[0].path == "age");
    assert(bad_errors[0].annotation == "Range");
    assert(bad_errors[3].path == "address.street");
    assert(bad_errors[4].path == "address.zip_code");

    // --- collect with FailFast ---
    const auto ff = av::collect(bad, av::Mode::FailFast);
    assert(ff.size() == 1);
    assert(ff[0].path == "age");

    // --- check ---
    const auto r_good = av::check(good);
    assert(r_good.has_value());

    const auto r_bad = av::check(bad);
    assert(!r_bad.has_value());
    assert(r_bad.error().size() == 5);

    // --- validate ---
    av::validate(good);  // no throw

    bool threw = false;
    try {
        av::validate(bad);
    } catch (const av::ValidationException& ex) {
        threw = true;
        assert(ex.errors.size() == 5);
    }
    assert(threw);

    // --- format_error spot check ---
    const auto line = av::format_error(bad_errors[0]);
    assert(line == "age: must be in [0, 150], got 200 (Range)");

    std::cout << "smoke test: OK\n";
    return 0;
}
