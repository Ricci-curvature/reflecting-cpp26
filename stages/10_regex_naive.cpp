// Stage 10: Regex validation, naive construction.
//
// Stage 9 read the pattern back through reflection but did nothing with it.
// Stage 10 wires it into a real validation path: for every Regex<N> annotation,
// build a std::regex from the inlined char[N] pattern and run regex_match
// against the field value.
//
// The build happens on every call — no caching anywhere. The point of this
// stage is to measure how bad that is. std::regex's constructor has to parse
// the pattern and build an NFA/DFA; std::regex_match on an already-built
// regex is much cheaper. We expect reconstruction to dominate.
//
// The benchmark loop at the bottom runs validate_all() N times and prints
// wall time per call, so Stage 11 (function-local static cache) and Stage 12
// (template-parameter cache) have a concrete baseline to beat.

#include <meta>
#include <iostream>
#include <string>
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
    std::string               no_annotation;
};

struct ValidationError {
    std::string path;
    std::string message;
    std::string annotation;
};

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
                constexpr auto args = std::define_static_array(
                    std::meta::template_arguments_of(
                        std::meta::type_of(ann)));
                constexpr std::size_t N =
                    std::meta::extract<std::size_t>(args[0]);
                constexpr auto r = std::meta::extract<Regex<N>>(ann);

                // The naive part: build a fresh std::regex every call.
                std::regex re{r.pattern};

                const auto& v = obj.[:member:];
                if (!std::regex_match(v, re)) {
                    errors.push_back({
                        std::string{std::meta::identifier_of(member)},
                        std::format("does not match /{}/", r.pattern),
                        "Regex"
                    });
                }
            }
        }
    }
}

int main() {
    // One-shot correctness run: a mix of matching and failing fields.
    Sample good{"alice", "12345", "a@b", ""};
    Sample bad {"Alice1", "12x45", "no-at-sign", ""};

    std::vector<ValidationError> errs;

    std::cout << "---- good sample ----\n";
    validate_all(good, errs);
    if (errs.empty()) {
        std::cout << "  (no errors)\n";
    } else {
        for (const auto& e : errs) {
            std::cout << "  " << e.path << ": " << e.message
                      << " (" << e.annotation << ")\n";
        }
    }
    errs.clear();

    std::cout << "---- bad sample ----\n";
    validate_all(bad, errs);
    for (const auto& e : errs) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }

    // Benchmark: validate() a passing sample many times, measure per-call cost.
    // 3 Regex annotations per call → 3 std::regex constructions per call.
    constexpr int iters = 50'000;
    std::vector<ValidationError> sink;
    sink.reserve(8);

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) {
        sink.clear();
        validate_all(good, sink);
    }
    auto t1 = std::chrono::steady_clock::now();

    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        t1 - t0).count();
    double per_call_ns = static_cast<double>(total_ns) / iters;
    double per_regex_ns = per_call_ns / 3.0;

    std::cout << "\n---- benchmark (naive: rebuild every call) ----\n";
    std::cout << "iterations     : " << iters << '\n';
    std::cout << "total          : " << total_ns / 1'000'000 << " ms\n";
    std::cout << "per validate() : " << std::format("{:.0f}", per_call_ns)
              << " ns  (3 regex constructions + 3 matches)\n";
    std::cout << "per regex op   : " << std::format("{:.0f}", per_regex_ns)
              << " ns  (construct + match)\n";

    // Reference point: construct once outside the loop, measure match alone.
    // This is what Stages 11/12 are aiming to get the per-call cost down to.
    std::regex re_name  {"^[a-z]+$"};
    std::regex re_digit {"^\\d+$"};
    std::regex re_email {"^\\S+@\\S+$"};

    auto t2 = std::chrono::steady_clock::now();
    volatile bool ok = true;
    for (int i = 0; i < iters; ++i) {
        ok &= std::regex_match(good.name,   re_name);
        ok &= std::regex_match(good.digits, re_digit);
        ok &= std::regex_match(good.email,  re_email);
    }
    auto t3 = std::chrono::steady_clock::now();
    auto match_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        t3 - t2).count();
    double per_match_ns = static_cast<double>(match_ns) / (iters * 3);

    std::cout << "\n---- reference (match only, pre-built regex) ----\n";
    std::cout << "per match      : " << std::format("{:.0f}", per_match_ns)
              << " ns\n";
    std::cout << "construction   : " << std::format("{:.1f}",
                      (per_regex_ns - per_match_ns) / per_match_ns)
              << "x match cost\n";

    return 0;
}
