#pragma once

#include <string_view>
#include <string>
#include <numeric>


template <typename Seq> // requires sequence_of_convertibles_to_string<Seq>
auto join_strings(Seq strings, std::string_view separator = ",") -> std::string
{
    return std::accumulate(strings.begin(), strings.end(), std::string(),
        [=](auto a, auto b) { return std::string{ a } + std::string{ a.length() > 0 ? separator : "" } + std::string{ b }; });
}

