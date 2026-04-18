// Stage 9: Reading a Regex annotation.
//
// Plan from v1's closing paragraph: store the pattern as a std::string_view
// on the annotation, construct the std::regex later. First attempt —
//
//     struct Regex { std::string_view pattern; /* ... */ };
//     [[=Regex{"^[a-z]+$"}]] std::string name;
//
// fails to compile:
//
//     error: C++26 annotation attribute requires an expression usable
//            as a template argument
//     note: pointer to subobject of string literal is not allowed in
//            a template argument
//
// P3394 annotation values must satisfy the NTTP rules. NTTP pointer rules
// forbid pointers to subobjects, and a std::string_view built from a string
// literal stores a const char* pointing at the first char of an array — a
// subobject. The view is rejected before it ever reaches reflection.
//
// Fix: inline the pattern as a fixed-size char array inside a templated
// Regex<N>, so each pattern-string becomes part of the type, not a pointer
// to external storage. Standard NTTP-string pattern. CTAD deduces N from
// the string literal at the annotation site.
//
// Read side then needs one more tool vs v1: the annotation type is no
// longer a single class but a family (Regex<8>, Regex<10>, …). The
// dispatch condition becomes "is this annotation's type an instantiation
// of the Regex template?" — std::meta::template_of(type_of(ann)) == ^^Regex.

#include <meta>
#include <iostream>
#include <string>
#include <cstddef>

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

int main() {
    template for (constexpr auto member :
                  std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^Sample, std::meta::access_context::unchecked())))
    {
        std::cout << "field: " << std::meta::identifier_of(member) << '\n';

        constexpr auto anns = std::define_static_array(
            std::meta::annotations_of(member));

        if constexpr (anns.size() == 0) {
            std::cout << "  (no annotations)\n";
        } else {
            template for (constexpr auto ann : anns) {
                if constexpr (std::meta::template_of(
                                  std::meta::type_of(ann)) == ^^Regex) {
                    constexpr auto args = std::define_static_array(
                        std::meta::template_arguments_of(
                            std::meta::type_of(ann)));
                    constexpr std::size_t N =
                        std::meta::extract<std::size_t>(args[0]);
                    constexpr auto r = std::meta::extract<Regex<N>>(ann);
                    std::cout << "  annotation: Regex { pattern=\""
                              << r.pattern << "\" }\n";
                }
            }
        }
    }
    return 0;
}
