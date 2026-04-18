# reflecting-cpp26

A header-only declarative validator for C++26, built on [P2996 reflection](https://wg21.link/p2996) and [P3394 annotations](https://wg21.link/p3394). The constraint lives on the field, not in a macro and not in a separate schema.

```cpp
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
```

Output:

```
age: must be in [0, 150], got 200 (Range)
name: length must be >= 3, got 2 (MinLength)
address.street: length must be >= 2, got 1 (MinLength)
address.zip_code: must be in [1, 99999], got 0 (Range)
```

## API

Everything lives under `namespace av`:

| Symbol | Role |
|---|---|
| `Range`, `MinLength`, `MaxLength`, `NotEmpty` | Annotation types attached with `[[=...]]` |
| `ValidationError` | `{ path, message, annotation }` |
| `ValidationException` | Thrown by `validate`, carries `std::vector<ValidationError>` |
| `Mode::CollectAll` / `Mode::FailFast` | Stop-policy passed to the three entry points |
| `collect(obj, mode)` | Returns `std::vector<ValidationError>` |
| `check(obj, mode)` | Returns `std::expected<void, std::vector<ValidationError>>` |
| `validate(obj, mode)` | Returns `void`; throws `ValidationException` on failure |
| `format_error(err)` | Renders `"<path>: <message> (<annotation>)"` |

`av::detail::` holds the internals (`ValidationContext`, `validate_impl`). Not user-facing, not API-stable.

## Requirements

- Bloomberg [clang-p2996](https://github.com/bloomberg/clang-p2996) fork
- Flags: `-std=c++26 -freflection-latest -stdlib=libc++`

No other library dependencies. Drop `include/validator.hpp` into your include path and you're done.

## Build + run the tests

```bash
clang++ -std=c++26 -freflection-latest -stdlib=libc++ \
    -I include -o tests/smoke_test.out tests/smoke_test.cpp
./tests/smoke_test.out
# smoke test: OK
```

The `stages/` directory holds the tutorial timeline — one `.cpp` per step from "hello reflection" to "header-only release". Each file is self-contained and matches a commit in the history.

## Walkthrough

[`blog/02-tutorial-building-validator.md`](blog/02-tutorial-building-validator.md) walks all eight stages in order, with the errors that showed up along the way and the clang-p2996 quirks that shaped the final design.

## Scope

This is a learning project, not a production library. The v1 core covers:

- Scalar and string fields with the four annotation types above
- Recursion into aggregate (plain-struct) members, producing dotted error paths
- Three entry-point policies (vector / expected / throw)
- `CollectAll` and `FailFast` stop modes, driven by the context — not by the policy wrapper

Deliberately out of scope for v1: `Regex`, `std::vector`/`std::optional` fields, custom validator callables, runtime-loaded schemas. Each of those is a separate post's worth of design.
