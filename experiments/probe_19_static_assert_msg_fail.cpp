// Probe 4b for Stage 19: when static_assert fails with a constexpr
// std::string message, does clang-p2996 emit the computed string
// in its diagnostic?
//
// This file is INTENTIONALLY UNCOMPILABLE. The driver script compiles
// it and greps stderr for MAGIC_MARKER. If the marker appears in the
// error, p2996 embeds the computed message in its diagnostic, and
// Stage 19 can surface per-annotation errors inline — e.g.
//   error: static assertion failed: age: must be in [0, 150], got 200 (Range)
// If the marker doesn't appear (compile fails but with a generic "static
// assertion failed" only), Stage 19 falls back to plain static_assert
// with no inline diagnostic.

#include <string>
#include <string_view>

constexpr std::string make_msg() {
    return std::string{"MAGIC_MARKER_STRING: constexpr std::string reached diagnostic"};
}

constexpr std::string_view make_sv() {
    return "MAGIC_MARKER_SV: constexpr string_view reached diagnostic";
}

static_assert(false, make_msg());
static_assert(false, make_sv());
static_assert(false, "MAGIC_MARKER_LITERAL: ordinary literal reached diagnostic");

int main() {}
