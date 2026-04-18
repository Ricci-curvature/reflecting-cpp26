// Stage 16 Q2 probe: capturing lambda as Predicate payload.
//
// This file is *not expected to compile*. Its only job is to produce
// a citeable clang-p2996 error for the blog post.
//
// Q2 hypothesis: a capturing lambda's closure type is non-structural
// (C++26 [temp.param]), so Predicate<ClosureType> — an instantiation
// forced by [[=Predicate{<capturing lambda>}]] — should be rejected
// at the NTTP / annotation-value level.
//
// The capture below is by-value of a constexpr global, so the capture
// itself is a constant expression. If the rejection is specifically
// about *having a capture at all* (not about the capture source
// being runtime), that isolates the cause to the structural-type rule.

#include <meta>

template <typename F>
struct Predicate {
    F f;
};

constexpr int threshold = 10;

struct BadField {
    // Capturing lambda below. The init-capture `[t = threshold]` creates
    // a fresh captured value (sidesteps the automatic-storage-only rule
    // that a plain [threshold] would hit on a global constexpr), so any
    // remaining rejection should be about structural-type, not capture
    // source.
    [[=Predicate{[t = threshold](int x) { return x > t; }}]]
    int value;
};

int main() {
    BadField bf{.value = 42};
    (void)bf;
    return 0;
}
