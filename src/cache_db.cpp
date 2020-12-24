#include <iostream>
#include <filesystem>
#include <string>
#include <regex>
#include <fmt/format.h>
#include "./cache_db.h"


static auto get_filename() {
#ifdef WIN32
    std::filesystem::path appdata_dir = getenv("LOCALAPPDATA");
    auto db_dir = appdata_dir / "ConanDB";
#else
    // TODO: TEST!
    appdata_dir = getenv("HOME");
    if (appdata_dir.empty()) {
        auto passwd = getpwuid(getuid());
        if (!passwd) throw std::runtime_error("Impossible to determine user home directory");
        appdata_dir = passwd->pw_dir;
    }
    auto db_dir = appdata_dir / ".ConanDB";
#endif
    auto filename = db_dir / "conan.sqlite";
    return filename.string();
}


Cache_db::Cache_db():
    Database{ get_filename().c_str() }
{
    sqlite3_create_function(
        handle(),
        "SEMVER_PART", 2,
        SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_DIRECTONLY,
        nullptr,
        [](sqlite3_context* context, int argc, sqlite3_value** argv) {
            static const auto re = std::regex("^(\\d+)\\.(\\d+)(?:\\.(\\d+)(?:[\\.\\-](\\d+))?)?$");
            if (argc == 2) {
                std::smatch m;
                std::string version{ (const char*)sqlite3_value_text(argv[0]) };
                int index = sqlite3_value_int(argv[1]);
                if (!(index >= 1 && index <= 4)) {
                    sqlite3_result_error(context, "The second parameter to SEMVER_PART() must be between 1 and 4", -1);
                    return;
                }
                if (std::regex_match(version, m, re)) {
                    auto value = m[index].matched ? std::stoi(m[index]) : 0;
                    // std::cout << version << " [" << index << "]: " << value << std::endl;
                    sqlite3_result_int(context, value);
                    return;
                }
            }
            sqlite3_result_null(context);
        },
        nullptr, nullptr
    );

    create_or_update();

    get_list_stmt = prepare_statement(R"(
        SELECT id, name, packages2.remote, user, channel, version, description, license, provides, author, topics 
        FROM packages2
        LEFT OUTER JOIN pkg_info ON pkg_info.pkg_id = packages2.id
        WHERE name LIKE ?1
        ORDER BY SUBSTR(name, 1, 1) COLLATE NOCASE ASC, name ASC, packages2.remote ASC, user ASC, channel ASC,
            SEMVER_PART(version, 1) DESC, SEMVER_PART(version, 2) DESC, SEMVER_PART(version, 3) DESC, SEMVER_PART(version, 4) DESC
    )");

    get_pkg_info = prepare_statement(R"(
        SELECT description, license, provides, author, topics, creation_date, last_poll
        FROM pkg_info
        WHERE pkg_id=?1;
    )");

    upsert_pkg_info = prepare_upsert(
        "pkg_info", 
        { "pkg_id"}, 
        { "description", "license", "provides", "author", "topics", "creation_date", "last_poll" }
    );
}

Cache_db::~Cache_db()
{
    sqlite3_finalize(get_list_stmt);
    sqlite3_finalize(get_pkg_info);
    sqlite3_finalize(upsert_pkg_info);
}

void Cache_db::create_or_update()
{
    auto version = std::get<int64_t>(select_one("PRAGMA user_version")[0]);
    std::cout << "version: " << version << std::endl;

    if (version >= 14) return;

    execute( R"(

        CREATE TABLE IF NOT EXISTS packages2 (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            remote STRING NOT NULL,
            name STRING NOT NULL,
            version STRING NOT NULL,
            user STRING,
            channel STRING,
            last_poll DATETIME
        );
        CREATE UNIQUE INDEX IF NOT EXISTS packages2_unique ON packages2(remote, name, version, user, channel);

        CREATE TABLE IF NOT EXISTS pkg_info (
            pkg_id INTEGER,
            recipe_id,
            remote STRING,
            url STRING,
            license STRING,
            description STRING,
            provides STRING,
            author STRING,
            topics STRING,
            creation_date DATETIME,
            last_poll DATETIME,
            FOREIGN KEY (pkg_id) REFERENCES packages2(id)
        );
        CREATE UNIQUE INDEX IF NOT EXISTS pkg_info_pkg_id ON pkg_info (pkg_id);

        PRAGMA user_version = 14;

    )", "trying to create packages2 table");
}

void Cache_db::get_list(std::function<bool(SQLite::Row)> row_cb, std::string_view name_filter)
{
    while (execute(get_list_stmt, { std::string{name_filter} })) {
        auto row = get_row(get_list_stmt);
        // std::cout << std::get<3>(row[1]) << std::endl;
        if (!row_cb(row)) break;
    }
}

void Cache_db::upsert_package(std::string_view remote, std::string_view name, std::string_view version, std::string_view user, std::string_view channel)
{
    // TODO: use prepared statement!

    auto statement = fmt::format(R"(
        INSERT INTO packages2 (remote, name, version, user, channel, last_poll)
            values('{0}', '{1}', '{2}', '{3}', '{4}', datetime('now'))
        ON CONFLICT (remote, name, version, user, channel) DO UPDATE SET last_poll=datetime('now');
    )", remote, name, version, user, channel);

    execute(statement.c_str(), "trying to upsert into package2");
}


auto Cache_db::get_package_info(int64_t pkg_id) -> std::optional<Package_info>
{
    if (execute(get_pkg_info, { pkg_id })) {
        auto row = get_row(get_pkg_info);
        return Package_info {
            .description   = std::get<3>(row[0]),
            .license       = std::get<3>(row[1]),
            .provides      = std::get<3>(row[2]),
            .author        = std::get<3>(row[3]),
            .topics        = std::get<3>(row[4]),
            .creation_date = std::get<3>(row[5]),
        };
    }
    return std::optional<Package_info>{};
}

void Cache_db::upsert_package_info(int64_t pkg_id, const Package_info& info)
{
    execute(upsert_pkg_info, { pkg_id, info.description, info.license, info.provides, info.author, info.topics, nullptr /* TODO */ });
}
