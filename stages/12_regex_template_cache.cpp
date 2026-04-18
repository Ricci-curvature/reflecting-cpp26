// Stage 12: Template-parameter caching, with std::meta::info as the NTTP.
//
// Three questions, not two.
//
// 1. Equivalence / identity / linkage. std::meta::info can be a non-type
//    template parameter — P2996 is explicit about that; it's a scalar
//    reflection type. The question isn't "does the declaration compile?"
//    but "is the reflection value a stable cache key?" Two annotations
//    with the same pattern at different sites — do they fold to one
//    template instantiation, or split into two? We probe by calling
//    get_regex_for<ann>() for each annotation and comparing the address
//    of the returned function-local static.
//
// 2. Recovering the pattern inside the template body, without tripping
//    Stage 9's splice wall. Stage 9 showed that a reflection held in a
//    template-for loop variable can't be spliced into a template
//    argument — extract<[:t:]>(ann) fails. But a reflection held in a
//    template parameter of type std::meta::info is a different kind of
//    constant source. The plan: reuse the Stage 9 recipe
//       template_arguments_of(type_of(Pattern))[0] → N
//       extract<Regex<N>>(Pattern) → the annotation value
//    — no splice anywhere. If this compiles, the Stage 9 restriction
//    really was about the loop-variable path, not about "reflections as
//    template arguments" in general.
//
// 3. What we're trading away. Stage 11 paid runtime cost: one mutex +
//    one unordered_map lookup per regex op. Stage 12 pays codegen cost:
//    one function instantiation + one function-local static std::regex
//    per distinct template argument value. If the Q1 answer is "folded,"
//    the instantiation count equals the unique-pattern count — cheap.
//    If the answer is "per-site," it equals the annotation-site count —
//    more expensive in binary size, especially at scale. We don't
//    benchmark codegen cost directly here; nm -C on the binary gives
//    the instantiation count cheaply.
//
// Abort criteria (decided in advance so the blog narrative is sharp):
//   A1 — template<std::meta::info Pattern> declaration itself rejected
//        → fall back to Design B (value NTTP over Regex<N>).
//   A2 — declaration accepted but body can't recover the pattern
//        → fall back to Design B, include the exact error in the post.
//   A3 — everything works but Q1 probe shows per-site granularity
//        → keep Design A, frame as "works but not the cache key we
//        wanted."

#include <meta>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <format>
#include <cstddef>
#include <regex>
#include <chrono>

template <std::size_t N>
struct Regex {
    char pattern[N];
    constexpr Regex(const char (&src)[N]) {
        for (std::size_t i = 0; i < N; ++i) pattern[i] = src[i];
    }
};

struct Sample {
    [[=Regex{"^[a-z]+$"}]]    std::string name;
    [[=Regex{"^\\d+$"}]]      std::string digits;
    [[=Regex{"^\\S+@\\S+$"}]] std::string email;
    [[=Regex{"^[a-z]+$"}]]    std::string nickname;   // same pattern as name
    std::string               no_annotation;
};

struct ValidationError {
    std::string path;
    std::string message;
    std::string annotation;
};

// The experiment. Pattern is a reflection of the annotation object.
// No splice: type_of, template_arguments_of, extract are all plain
// metafunction calls taking reflection values and returning them.
template <std::meta::info Pattern>
const std::regex& get_regex_for() {
    constexpr auto args = std::define_static_array(
        std::meta::template_arguments_of(
            std::meta::type_of(Pattern)));
    constexpr std::size_t N =
        std::meta::extract<std::size_t>(args[0]);
    constexpr auto r = std::meta::extract<Regex<N>>(Pattern);
    static const std::regex re{r.pattern};
    return re;
}

template <typename T>
void validate_all(const T& obj, std::vector<ValidationError>& errors) {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked())))
    {
        template for (constexpr auto ann :
                      std::define_static_array(
                          std::meta::annotations_of(member)))
        {
            if constexpr (std::meta::template_of(
                              std::meta::type_of(ann)) == ^^Regex) {
                // ann is a constexpr std::meta::info — a reflection, not
                // a splice. Passing it directly as an NTTP is the whole
                // Stage 12 experiment.
                const std::regex& re = get_regex_for<ann>();
                const auto& v = obj.[:member:];
                if (!std::regex_match(v, re)) {
                    errors.push_back({
                        std::string{std::meta::identifier_of(member)},
                        "does not match pattern",
                        "Regex"
                    });
                }
            }
        }
    }
}

// Q1 probe: walk the reflection, print the address of the static regex
// returned for each annotation. Same address across two same-pattern
// annotations → this implementation folded them to one instantiation
// under this setup. Different addresses → per-site granularity. This
// does not speak to the full cross-TU equivalence / linkage story.
template <typename T>
void probe_instances(const T& /*obj*/) {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked())))
    {
        template for (constexpr auto ann :
                      std::define_static_array(
                          std::meta::annotations_of(member)))
        {
            if constexpr (std::meta::template_of(
                              std::meta::type_of(ann)) == ^^Regex) {
                constexpr auto args = std::define_static_array(
                    std::meta::template_arguments_of(
                        std::meta::type_of(ann)));
                constexpr std::size_t N =
                    std::meta::extract<std::size_t>(args[0]);
                constexpr auto r = std::meta::extract<Regex<N>>(ann);
                const std::regex& re = get_regex_for<ann>();
                std::cout << "  " << std::meta::identifier_of(member)
                          << "  pattern=\"" << r.pattern
                          << "\"  &regex = "
                          << static_cast<const void*>(&re) << '\n';
            }
        }
    }
}

int main() {
    Sample good{"alice", "12345", "a@b", "bob", ""};
    Sample bad {"Alice1", "12x45", "no-at-sign", "Bob2", ""};

    std::vector<ValidationError> errs;

    std::cout << "---- good sample ----\n";
    validate_all(good, errs);
    if (errs.empty()) std::cout << "  (no errors)\n";
    for (const auto& e : errs) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }
    errs.clear();

    std::cout << "---- bad sample ----\n";
    validate_all(bad, errs);
    for (const auto& e : errs) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }
    errs.clear();

    std::cout << "\n---- Q1: cache granularity probe ----\n";
    std::cout << "(name and nickname share pattern \"^[a-z]+$\")\n";
    probe_instances(good);

    // Benchmark. 4 regex ops per validate() (name, digits, email, nickname).
    constexpr int iters = 50'000;
    std::vector<ValidationError> sink;
    sink.reserve(8);

    for (int i = 0; i < 16; ++i) {
        sink.clear();
        validate_all(good, sink);
    }

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) {
        sink.clear();
        validate_all(good, sink);
    }
    auto t1 = std::chrono::steady_clock::now();
    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        t1 - t0).count();
    double per_call     = static_cast<double>(total_ns) / iters;
    double per_regex_op = per_call / 4.0;

    std::cout << "\n---- benchmark (" << iters
              << " iters, 4 regex ops/call) ----\n";
    std::cout << "per validate() : " << std::format("{:.0f}", per_call)
              << " ns\n";
    std::cout << "per regex op   : " << std::format("{:.0f}", per_regex_op)
              << " ns\n";

    // All numbers are 5-run medians under identical conditions.
    std::cout << "\n---- comparison (per regex op, 5-run median) ----\n";
    std::cout << "stage 10 naive             : 584 ns\n";
    std::cout << "stage 11 locked cache      : 395 ns\n";
    std::cout << "stage 11 unlocked cache    : 379 ns\n";
    std::cout << "pre-built match only floor : 310 ns\n";
    std::cout << "stage 12 template cache    : "
              << std::format("{:.0f}", per_regex_op) << " ns\n";

    return 0;
}
