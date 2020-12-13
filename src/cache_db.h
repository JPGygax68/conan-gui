#pragma once

#include "./sqlite_wrapper/database.h"


class Cache_db: public SQLite::Database {
public:
    Cache_db();

    void create_or_update();
};