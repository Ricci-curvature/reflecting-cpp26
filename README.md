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
| `Range`, `MinLength`, `MaxLength`, `NotEmpty` | Scalar/string annotations attached with `[[=...]]` |
| `MinSize`, `MaxSize` | Container-size bounds for `std::vector<T>` fields |
| `NotNullopt` | Asserts a `std::optional<T>` field has a value |
| `Predicate<F, N>` | User-callable annotation: captureless lambda + `char[N]` message. CTAD: `Predicate{f}` for the default message, `Predicate{f, "..."}` for a custom literal |
| `ValidationError` | `{ path, message, annotation }` |
| `ValidationException` | Thrown by `validate`, carries `std::vector<ValidationError>` |
| `Mode::CollectAll` / `Mode::FailFast` | Stop-policy passed to the three entry points |
| `collect(obj, mode)` | Returns `std::vector<ValidationError>` |
| `check(obj, mode)` | Returns `std::expected<void, std::vector<ValidationError>>` |
| `validate(obj, mode)` | Returns `void`; throws `ValidationException` on failure |
| `passes(obj)` | `constexpr bool` — true iff no annotation rejected. Usable at compile time |
| `first_error(obj)` | `constexpr std::string` — `"<path>: <message> (<annotation>)"` for the first error, empty string if none |
| `format_error(err)` | Renders `"<path>: <message> (<annotation>)"` |

Every annotation in the table is protocol-based — it owns a `constexpr template <V, Ctx> void validate(const V&, Ctx&)` member, and the walker calls into it through a `requires { a.validate(v, ctx); }` guard. That means user code can introduce its own annotations the same way: write a plain struct with a `validate()` member and attach it with `[[=MyAnnotation{}]]`. The library header does not need to change.

`passes` and `first_error` are the compile-time entry points. They're `constexpr`, so they work at both consteval and runtime:

```cpp
constexpr User alice{.age = 30, .name = std::string{"Alice"},
                     .address = Address{.street = std::string{"Main St"},
                                        .zip_code = 12345}};
static_assert(av::passes(alice), av::first_error(alice));
```

A failing assertion surfaces the full computed message in the diagnostic (P2741), for instance `error: static assertion failed: age: must be in [0, 150], got 200 (Range)`. Namespace-scope `constexpr` works for types whose members have no transient heap past their initializer (e.g. short SSO strings); for types containing `std::vector<T>` construct inside a `consteval` helper and call `passes` / `first_error` there.

`av::detail::` holds the internals (`ValidationContext`, `walk_members`, `dispatch_value`, the `is_optional_v` / `is_vector_v` traits, a `to_str` mini-formatter that replaces `std::format` at consteval). Not user-facing, not API-stable.

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

clang++ -std=c++26 -freflection-latest -stdlib=libc++ \
    -I include -o tests/protocol_smoke_test.out tests/protocol_smoke_test.cpp
./tests/protocol_smoke_test.out
# protocol smoke test: OK

clang++ -std=c++26 -freflection-latest -stdlib=libc++ \
    -I include -o tests/constexpr_smoke_test.out tests/constexpr_smoke_test.cpp
./tests/constexpr_smoke_test.out
# constexpr smoke test: OK
```

`smoke_test.cpp` covers the Stage 8 public surface (scalar/string annotations, aggregate recursion, the three entry-point policies, `FailFast`). `protocol_smoke_test.cpp` covers the Stage 18 additions exercised through the same header: `std::optional<T>` / `std::vector<T>` wrapper recursion with indexed paths, `MinSize` / `MaxSize` / `NotNullopt`, `Predicate` with default and custom messages, and a user-defined protocol annotation picked up by the walker without any library change. `constexpr_smoke_test.cpp` covers Stage 19: `static_assert(av::passes(obj), av::first_error(obj))` on literal structs — both directly at namespace scope and through a `consteval` helper for `std::vector`-containing types — with the exact failure messages pinned at compile time against the same string the runtime walker produces.

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
- **18** — [validator protocol: annotation carries its own `validate()`](https://github.com/Ricci-curvature/reflecting-cpp26/blob/d952c2b/stages/18_validator_protocol.cpp)
- **19** — [constexpr validation: `static_assert` reads the error](https://github.com/Ricci-curvature/reflecting-cpp26/blob/fd89528/stages/19_constexpr_validation.cpp)

*Heads-up: pinned snapshots 1–7 predate the comment translation and still show the original Korean. Current state in [`755c15d`](https://github.com/Ricci-curvature/reflecting-cpp26/commit/755c15d) is English.*

## Walkthroughs

- [**A Declarative Validator in C++26**](https://riccilab.dev/blog/A-Declarative-Validator-in-C++26) walks stages 1–8, with the errors that showed up along the way and the clang-p2996 quirks that shaped the final design.
- [**Caching Regex with C++26 Reflection**](https://riccilab.dev/blog/Caching-Regex-with-C++26-Reflection) covers stages 9–12: adding a `Regex<N>` annotation, measuring the naive rebuild cost, and comparing a function-local static cache against a template-parameter cache keyed on `std::meta::info` as an NTTP.
- [**Validating Containers with C++26 Reflection**](https://riccilab.dev/blog/Validating-Containers-with-C++26-Reflection) covers stages 13–15: extending the walker past `is_aggregate_v` to recurse into `std::optional<T>` and `std::vector<T>`, switching the path stack to `std::variant<std::string, std::size_t>` so indices render as `[N]`, and adding container-level `MinSize` / `MaxSize` annotations.
- [**Opening a Closed Annotation Set with Structural Lambdas**](https://riccilab.dev/blog/Opening-a-Closed-Annotation-Set-with-Structural-Lambdas) covers stage 16: opening the closed-set dispatch with one `Predicate<F>` wrapper branch, confirming captureless lambda closures work as structural NTTPs, isolating the structural-type rule that rejects capturing closures, and contrasting per-site closure-type identity against stage 12's value-NTTP fold.
- [**One Refactor, Three Payoffs**](https://riccilab.dev/blog/One-Refactor-Three-Payoffs) covers stage 17: splitting the walker into `walk_members<T>` and `dispatch_value<Member, V>` so the annotation ladder runs against the value in hand at every level. The refactor un-skips stage 13's `optional<Scalar>+Range` and stage 14's `vector<Scalar>+Range` without new syntax, composes `vector<optional<Aggregate>>` walks out of the same dispatch, and turns two `Predicate` annotations with different callable signatures into container-level vs element-level checks on the same field.
- [**Annotation IS the Validator**](https://riccilab.dev/blog/Annotation-IS-the-Validator) covers stage 18: collapsing the six-branch annotation ladder into a single protocol probe. Every annotation — `Range`, `MinLength`, `Predicate`, and anything the user writes — carries its own `validate(v, ctx)` member, and the walker calls it through a `requires { a.validate(v, ctx); }` guard and knows nothing else. `Predicate` gains a `char[N]` message field with CTAD deduction guides for default vs custom literals, and user-defined annotations ride the same wrapper-piercing recursion as the built-ins — a plain struct with a `validate()` member is all it takes.
- [**From Stage to Library**](https://riccilab.dev/blog/from-stage-to-library) covers the migration of the Stage 18 protocol into `include/validator.hpp`: the dispatch collapse really does fit in one `requires { a.validate(v, ctx); }`, but reconciling Stage 14's indexed path stack, Stage 17's wrapper-piercing recursion, and Stage 7's `FailFast` policy into one walker meant re-gating `should_stop()` at recursion boundaries the Stage 8 header never had. The Stage 8 public API stayed intact — the existing smoke test runs unchanged — while a new `protocol_smoke_test.cpp` covers the optional / vector / `Predicate` / user-defined-protocol surface through the same header.
- [**static_assert Reads the Error**](https://riccilab.dev/blog/static-assert-reads-the-error) covers stage 19: lifting the walker and every `validate()` body into `constexpr`, swapping `std::format` for a `std::to_chars` mini-formatter so messages compose at consteval, and exposing `av::passes(obj)` / `av::first_error(obj)` as two new entry points. Four probes pin down the path — `std::format` is still non-`constexpr` under this libc++, the walker itself (template for, splices, `extract`, transient `vector<ValidationError>`) runs clean at consteval, `std::to_chars` reproduces byte-identical message bytes, and P2741's user-defined `static_assert` message renders a computed `std::string` straight into the diagnostic. `static_assert(av::passes(obj), av::first_error(obj))` on a bad literal surfaces as `error: static assertion failed: age: must be in [0, 150], got 200 (Range)` at translation time, with no runtime side.

## Scope

This is a learning project, not a production library. The header-only core (`include/validator.hpp`) covers:

- Scalar, string, `std::optional<T>`, and `std::vector<T>` fields with a set of protocol-based annotations (`Range`, `MinLength`, `MaxLength`, `NotEmpty`, `MinSize`, `MaxSize`, `NotNullopt`, `Predicate<F, N>`)
- Recursion into aggregate (plain-struct) members, producing dotted error paths with `[N]` for container indices (`users[2].address.zip_code`)
- User-defined annotations through the same protocol — any struct with a `constexpr template <V, Ctx> void validate(const V&, Ctx&)` member rides the same dispatch
- Three runtime entry-point policies (vector / expected / throw)
- Two compile-time entry points (`passes` / `first_error`) for `static_assert` on literal structs, with P2741 user-defined assertion messages surfacing the full error string in the diagnostic
- `CollectAll` and `FailFast` stop modes, driven by the context — not by the policy wrapper

Stages 9–12 extend the experiment with a `Regex<N>` annotation and compare runtime vs compile-time caching strategies for `std::regex`. Those stages are standalone `.cpp` files and not yet merged into the header-only library. Still out of scope: runtime-loaded schemas, annotation-driven serialization — each is a separate post's worth of design.
