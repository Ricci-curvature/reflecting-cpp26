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

## Stages

The repo is organised as a staged build log. Every stage is a single self-contained `.cpp` file, pinned to the commit where it was first checked in — so each link is the file *as it existed when that step was the head of the project*, not after later polish.

- **01** — [hello reflection](https://github.com/Ricci-curvature/reflecting-cpp26/blob/8f1011f/stages/01_hello_reflection.cpp)
- **02** — [annotation reading](https://github.com/Ricci-curvature/reflecting-cpp26/blob/2eed156/stages/02_annotation_reading.cpp)
- **03** — [first validator](https://github.com/Ricci-curvature/reflecting-cpp26/blob/070c662/stages/03_first_validator.cpp)
- **04** — [validation context](https://github.com/Ricci-curvature/reflecting-cpp26/blob/36d60f2/stages/04_validation_context.cpp)
- **05** — [string annotations](https://github.com/Ricci-curvature/reflecting-cpp26/blob/32d79f3/stages/05_string_annotations.cpp)
- **06** — [nested structs](https://github.com/Ricci-curvature/reflecting-cpp26/blob/afe66a0/stages/06_nested_structs.cpp)
- **07** — [policy layer](https://github.com/Ricci-curvature/reflecting-cpp26/blob/e98d973/stages/07_policy_layer.cpp)
- **08** — [header-only](https://github.com/Ricci-curvature/reflecting-cpp26/blob/efde80c/stages/08_header_only.cpp)
- **09** — [regex annotation, read-only](https://github.com/Ricci-curvature/reflecting-cpp26/blob/643d6ee/stages/09_regex_annotation_read.cpp)
- **10** — [regex validation, naive per-call construction](https://github.com/Ricci-curvature/reflecting-cpp26/blob/1c35c6e/stages/10_regex_naive.cpp)
- **11** — [regex function-local static cache (map + mutex)](https://github.com/Ricci-curvature/reflecting-cpp26/blob/2df52e4/stages/11_regex_static_cache.cpp)
- **12** — [regex template-parameter cache via `std::meta::info` NTTP](https://github.com/Ricci-curvature/reflecting-cpp26/blob/df3f034/stages/12_regex_template_cache.cpp)
- **13** — [`std::optional<T>` recursion + `NotNullopt`](https://github.com/Ricci-curvature/reflecting-cpp26/blob/48bc245/stages/13_optional_field.cpp)
- **14** — [`std::vector<T>` recursion + `path_stack` variant](https://github.com/Ricci-curvature/reflecting-cpp26/blob/4cf6353/stages/14_vector_field.cpp)
- **15** — [container-level `MinSize` / `MaxSize`](https://github.com/Ricci-curvature/reflecting-cpp26/blob/3a64156/stages/15_container_annotations.cpp)
- **16** — [custom predicate annotations via `Predicate<F>` wrapper](https://github.com/Ricci-curvature/reflecting-cpp26/blob/02cc12c/stages/16_custom_predicates.cpp)
- **17** — [dispatch refactor: annotation ladder travels through wrappers](https://github.com/Ricci-curvature/reflecting-cpp26/blob/9d44e99/stages/17_dispatch_refactor.cpp)

*Heads-up: pinned snapshots 1–7 predate the comment translation and still show the original Korean. Current state in [`755c15d`](https://github.com/Ricci-curvature/reflecting-cpp26/commit/755c15d) is English.*

## Walkthroughs

- [**A Declarative Validator in C++26**](https://riccilab.dev/blog/A-Declarative-Validator-in-C++26) walks stages 1–8, with the errors that showed up along the way and the clang-p2996 quirks that shaped the final design.
- [**Caching Regex with C++26 Reflection**](https://riccilab.dev/blog/Caching-Regex-with-C++26-Reflection) covers stages 9–12: adding a `Regex<N>` annotation, measuring the naive rebuild cost, and comparing a function-local static cache against a template-parameter cache keyed on `std::meta::info` as an NTTP.
- [**Validating Containers with C++26 Reflection**](https://riccilab.dev/blog/Validating-Containers-with-C++26-Reflection) covers stages 13–15: extending the walker past `is_aggregate_v` to recurse into `std::optional<T>` and `std::vector<T>`, switching the path stack to `std::variant<std::string, std::size_t>` so indices render as `[N]`, and adding container-level `MinSize` / `MaxSize` annotations.
- [**Opening a Closed Annotation Set with Structural Lambdas**](https://riccilab.dev/blog/Opening-a-Closed-Annotation-Set-with-Structural-Lambdas) covers stage 16: opening the closed-set dispatch with one `Predicate<F>` wrapper branch, confirming captureless lambda closures work as structural NTTPs, isolating the structural-type rule that rejects capturing closures, and contrasting per-site closure-type identity against stage 12's value-NTTP fold.
- [**One Refactor, Three Payoffs**](https://riccilab.dev/blog/One-Refactor-Three-Payoffs) covers stage 17: splitting the walker into `walk_members<T>` and `dispatch_value<Member, V>` so the annotation ladder runs against the value in hand at every level. The refactor un-skips stage 13's `optional<Scalar>+Range` and stage 14's `vector<Scalar>+Range` without new syntax, composes `vector<optional<Aggregate>>` walks out of the same dispatch, and turns two `Predicate` annotations with different callable signatures into container-level vs element-level checks on the same field.

## Scope

This is a learning project, not a production library. The header-only core (`include/validator.hpp`) covers:

- Scalar and string fields with the four annotation types above
- Recursion into aggregate (plain-struct) members, producing dotted error paths
- Three entry-point policies (vector / expected / throw)
- `CollectAll` and `FailFast` stop modes, driven by the context — not by the policy wrapper

Stages 9–12 extend the experiment with a `Regex<N>` annotation and compare runtime vs compile-time caching strategies for `std::regex`. Stages 13–15 add `std::optional<T>` and `std::vector<T>` recursion on top of the aggregate walker, switch the path stack from a flat `vector<string>` to `vector<variant<string, size_t>>` so indices stay structurally distinct from field names, and introduce container-level `MinSize` / `MaxSize` annotations. All four stages are standalone `.cpp` files and not yet merged into the header-only library. Still out of scope: custom validator callables, runtime-loaded schemas — each is a separate post's worth of design.
