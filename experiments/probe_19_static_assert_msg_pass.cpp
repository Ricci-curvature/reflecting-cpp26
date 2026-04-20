// Probe 4a for Stage 19: does clang-p2996 accept a constexpr std::string
// as the message argument of static_assert (C++26 P2741)?
//
// This file should compile clean. If it does, the syntax form
//     static_assert(cond, non_literal_constexpr_string_expr)
// is accepted. The companion _fail.cpp then checks whether the string
// is actually embedded in the compiler's diagnostic on failure.

#include <string>

constexpr std::string make_msg() {
    return std::string{"custom static_assert message from constexpr string"};
}

static_assert(true, make_msg());

// Also try a string_view form, since some implementations accept that
// but not std::string.
constexpr std::string_view make_sv() {
    return "custom static_assert message from constexpr string_view";
}

static_assert(true, make_sv());

// And a char-array form.
static_assert(true, "ordinary string literal");

int main() {}
