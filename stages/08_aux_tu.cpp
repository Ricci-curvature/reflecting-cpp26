// Stage 8: ODR smoke-test aux TU.
// Purpose: include <validator.hpp> from a second translation unit and define a
// trivial function that uses the public API. If the header had non-inline
// free functions or non-template non-member definitions without a namespace,
// linking this with stages/08_header_only.cpp would produce
// "multiple definition of ..." errors. Both TUs compiling and linking clean
// is the ODR proof.

#include "../include/validator.hpp"

#include <string>

namespace {

struct Thing {
    [[=av::MinLength{1}]] std::string label;
};

}  // namespace

// Non-template free function — defined in *this* TU, not in the header.
// Exercises `av::format_error` (inline), `av::collect` (template), and
// `av::Mode` / `av::ValidationError` from the header.
std::size_t count_errors_in_aux_tu() {
    Thing t{""};
    auto errs = av::collect(t);
    for (const auto& e : errs) {
        (void)av::format_error(e);
    }
    return errs.size();
}
