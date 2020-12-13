#pragma once

#include <variant>
#include <string>
#include <vector>
#include <cstdint>


namespace SQLite {

    using Blob = std::vector<uint8_t>;

    using Value = std::variant<
        nullptr_t,
        int64_t,
        double,
        std::string,    // TODO: use unsigned char string as SQLite does ?
        Blob
    >;

} // ns SQLite