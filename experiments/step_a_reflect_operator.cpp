// Step A smoke: verify the ^^ reflect operator parses and yields a constant.
// No library headers needed — pure language-level check.

consteval bool reflect_check() {
    auto r = ^^int;
    return r == ^^int;
}

static_assert(reflect_check());

int main() { return 0; }
