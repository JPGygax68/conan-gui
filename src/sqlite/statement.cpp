#include "statement.h"


namespace SQLite {

    auto Statement::upsert(
        std::string_view table, 
        std::initializer_list<std::string_view> unique_columns, 
        std::initializer_list<std::string_view> extra_columns, 
        std::initializer_list<Value> values
    ) -> Statement
    {
        return Statement();
    }

} // ns SQLite