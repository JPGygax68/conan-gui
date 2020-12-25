#include <stdexcept>
#include <fmt/format.h>
#include "./string_utils.h"


auto parseTagList(std::string_view text) -> std::vector<std::string>
{
    // using namespace std::string_literals;

    std::vector<std::string> list;

    if (text.empty()) return list;

    auto it = text.begin();

    if (text.front() == '(' && text.back() == ')')
        ++it;
    else if (text == "None")
        return list;
    // else
    //    throw std::runtime_error(fmt::format("Tag list has incorrect (unparsable) format: {}", text));

    for (;;) {
        while (isspace(*it)) it++;
        //if (*it != '\'')
        //    throw std::runtime_error(fmt::format("Expected ' at position {0} of \"{1}\"", it - text.begin(), text));
        ++it;
        auto i1{ it };
        if (*it == '\'') ++it;
        while (*i1 == '\'' && *it != '\'' || it != end(text) && *it != ',') {
            if (*it == '\\') ++it;
            ++it;
        }
        list.push_back(std::string{ i1, it });
        if (*i1 == '\'') ++it;
        if (it == end(text))
            break;
        while (isspace(*it)) it++;
        if (*it == ',')
            ++it;
        else if (text[0] == '(' && *it == ')')
            break;
        else
            throw std::runtime_error(fmt::format("Expected , or ) at position {0} of \"{1}\"", it - text.begin(), text));
    }

    return list;
}
