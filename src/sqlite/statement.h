#pragma once

#include <string_view>
#include "./types.h"


namespace SQLite {

    class Statement {

    public:
        static auto upsert(
            std::string_view table,
            std::initializer_list<std::string_view> unique_columns,
            std::initializer_list<std::string_view> extra_columns,
            std::initializer_list<Value> values
        ) -> Statement;

    };

} // ns SQLite