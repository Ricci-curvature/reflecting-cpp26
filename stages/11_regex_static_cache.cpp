// Stage 11: Function-local static cache for std::regex.
//
// Stage 10 rebuilt a std::regex on every call — ~530 ns per regex op,
// roughly half of which was construction. Stage 11 builds each unique
// pattern exactly once and hands back a reference thereafter.
//
// Storage: function-local static unordered_map<string, regex>. The key is
// the pattern string, so the lookup is a runtime hash. This is the
// "boring safe answer" in v2.md — Stage 12 will replace the map with a
// per-template-instantiation static, moving the key to compile time.
//
// Thread safety: the map is shared mutable state. A first-seen pattern
// writes while a later call may already be reading. Wrap access in a
// std::mutex. For contrast we also benchmark an unlocked version: it's
// unsafe in the multi-threaded case but gives a ceiling for "what if the
// lock were free?" so we can see the uncontended mutex cost.

#include <meta>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <format>
#include <cstddef>
#include <regex>
#include <chrono>
#include <unordered_map>
#include <mutex>

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

using CacheFn = const std::regex& (*)(std::string_view);

// Thread-safe version. The map and the mutex are both function-local
// statics — initialized once, by C++11 magic-static rules, on first call.
inline const std::regex& get_cached_regex_locked(std::string_view pattern) {
    static std::unordered_map<std::string, std::regex> cache;
    static std::mutex mtx;
    std::lock_guard lock(mtx);
    auto it = cache.find(std::string{pattern});
    if (it == cache.end()) {
        it = cache.emplace(
            std::string{pattern},
            std::regex{std::string{pattern}}
        ).first;
    }
    return it->second;
}

// Reference ceiling: no lock. Unsafe under concurrent access, used only
// to measure what the locked version would cost with a free mutex.
inline const std::regex& get_cached_regex_unlocked(std::string_view pattern) {
    static std::unordered_map<std::string, std::regex> cache;
    auto it = cache.find(std::string{pattern});
    if (it == cache.end()) {
        it = cache.emplace(
            std::string{pattern},
            std::regex{std::string{pattern}}
        ).first;
    }
    return it->second;
}

template <typename T>
void validate_all(const T& obj, std::vector<ValidationError>& errors,
                  CacheFn fetch) {
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

                const std::regex& re = fetch(r.pattern);
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
    Sample good{"alice", "12345", "a@b", ""};
    Sample bad {"Alice1", "12x45", "no-at-sign", ""};

    std::vector<ValidationError> errs;

    std::cout << "---- good sample (locked cache) ----\n";
    validate_all(good, errs, get_cached_regex_locked);
    if (errs.empty()) {
        std::cout << "  (no errors)\n";
    } else {
        for (const auto& e : errs) {
            std::cout << "  " << e.path << ": " << e.message
                      << " (" << e.annotation << ")\n";
        }
    }
    errs.clear();

    std::cout << "---- bad sample (locked cache) ----\n";
    validate_all(bad, errs, get_cached_regex_locked);
    for (const auto& e : errs) {
        std::cout << "  " << e.path << ": " << e.message
                  << " (" << e.annotation << ")\n";
    }
    errs.clear();

    constexpr int iters = 50'000;
    std::vector<ValidationError> sink;
    sink.reserve(8);

    // Warmup: populate both caches so the timed loops see only hits.
    for (int i = 0; i < 16; ++i) {
        sink.clear();
        validate_all(good, sink, get_cached_regex_locked);
        sink.clear();
        validate_all(good, sink, get_cached_regex_unlocked);
    }

    // Locked cache — the real, thread-safe number.
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) {
        sink.clear();
        validate_all(good, sink, get_cached_regex_locked);
    }
    auto t1 = std::chrono::steady_clock::now();
    auto locked_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         t1 - t0).count();

    // Unlocked cache — reference ceiling.
    auto t2 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) {
        sink.clear();
        validate_all(good, sink, get_cached_regex_unlocked);
    }
    auto t3 = std::chrono::steady_clock::now();
    auto unlocked_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           t3 - t2).count();

    double locked_per_call   = static_cast<double>(locked_ns)   / iters;
    double unlocked_per_call = static_cast<double>(unlocked_ns) / iters;

    std::cout << "\n---- benchmark (cache hits, " << iters << " iters) ----\n";
    std::cout << "locked   (mutex + map lookup) : "
              << std::format("{:.0f}", locked_per_call)
              << " ns/validate   ("
              << std::format("{:.0f}", locked_per_call / 3.0)
              << " ns/regex)\n";
    std::cout << "unlocked (map lookup only)    : "
              << std::format("{:.0f}", unlocked_per_call)
              << " ns/validate   ("
              << std::format("{:.0f}", unlocked_per_call / 3.0)
              << " ns/regex)\n";
    std::cout << "lock cost per validate        : "
              << std::format("{:.0f}", locked_per_call - unlocked_per_call)
              << " ns\n";

    std::cout << "\n---- comparison ----\n";
    std::cout << "stage 10 naive (measured)     : 1589 ns/validate\n";
    std::cout << "stage 11 locked               : "
              << std::format("{:.0f}", locked_per_call) << " ns/validate\n";
    std::cout << "speedup                       : "
              << std::format("{:.2f}", 1589.0 / locked_per_call) << "x\n";

    return 0;
}
