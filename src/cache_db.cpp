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
            static const auto re = std::regex("^(\\d+)\\.(\\d+)\\.(\\d+)(?:[\\.\\-](\\d+))$");
            if (argc == 2) {
                std::smatch m;
                std::string version{ (const char*)sqlite3_value_text(argv[0]) };
                int index = sqlite3_value_int(argv[1]);
                if (!(index >= 1 && index <= 4)) {
                    sqlite3_result_error(context, "The second parameter to SEMVER_PART() must be between 1 and 3", -1);
                    return;
                }
                if (std::regex_match(version, m, re)) {
                    sqlite3_result_int(context, std::stoi(m[index]));
                    return;
                }
            }
            sqlite3_result_null(context);
        },
        nullptr, nullptr
    );

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
    // TODO: unprepare in dtor
}

Cache_db::~Cache_db()
{
    sqlite3_finalize(get_pkg_info);
    sqlite3_finalize(upsert_pkg_info);
}

void Cache_db::create_or_update()
{
    // char* errmsg;
    // int db_err = 0;
    // 
    // unsigned version = 0;
    // db_err = sqlite3_exec(db_handle, "PRAGMA user_version",
    //     [](void* version_, int coln, char* textv[], char* namev[]) -> int {
    //         auto pver = static_cast<decltype(version)*>(version_);
    //         *pver = std::stoi(textv[0]);
    //         return 0;
    //     }, &version, nullptr);
    // if (db_err != 0) throw sqlite_error(db_handle, db_err, "trying to retrieve user_version");

    auto version = std::get<int64_t>(select_one("PRAGMA user_version")[0]);

    std::cout << "version: " << version << std::endl;

#ifdef NOT_DEFINED

    db_err = sqlite3_exec(db_handle, R"(
            create table if not exists packages (
                remote STRING,
                reference STRING,
                last_poll DATETIME
            );
        )", nullptr, nullptr, &errmsg);
    if (db_err != 0) throw sqlite_error(db_handle, db_err, "trying to create packages table");

    if (version == 0) {
        db_err = sqlite3_exec(db_handle, R"(
                drop table if exists packages_old;
                create table packages_old as select * from packages;
                drop table packages;
                create table packages as select * from packages_old;
                pragma user_version = 1;
            )", nullptr, nullptr, &errmsg);
        if (db_err != 0) throw sqlite_error(db_handle, db_err, "trying to add primary key to packages table");
    }

    db_err = sqlite3_exec(db_handle, R"(
            create unique index if not exists remote_reference on packages (remote, reference);
        )", nullptr, nullptr, &errmsg);
    if (db_err != 0) throw sqlite_error(db_handle, db_err, "trying to create index remote_reference on packages table");

    db_err = sqlite3_exec(db_handle, R"(
            create table if not exists package_info (
                remote STRING,
                reference STRING,
                last_poll DATETIME
            );
        )", nullptr, nullptr, &errmsg);
    if (db_err != 0) throw sqlite_error(db_handle, db_err, "trying to create packages table");

    if (version == 12) {
        exec(R"(
                BEGIN TRANSACTION;
                CREATE TABLE packages2_backup AS SELECT id, remote, name, version, user, channel, last_poll FROM packages2;
                DROP TABLE packages2;
                CREATE TABLE packages2 (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    remote STRING NOT NULL,
                    name STRING NOT NULL,
                    version STRING NOT NULL,
                    user STRING,
                    channel STRING,
                    last_poll DATETIME
                );
                INSERT INTO packages2 SELECT * FROM packages2_backup;
                DROP TABLE packages2_backup;
                COMMIT;
            )", "trying to remove SEMVER fields from \"packages2\"");
    }
    else {
        exec(R"(
                create table if not exists packages2 (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    remote STRING NOT NULL,
                    name STRING NOT NULL,
                    version STRING NOT NULL,
                    user STRING,
                    channel STRING,
                    last_poll DATETIME
                )
            )", "trying to create packages2 table");
    }
    if (version <= 5 || version == 12)
        exec("create unique index if not exists packages2_unique on packages2(remote, name, version, user, channel)");

    if (version == 9) {
        exec(R"(
                alter table packages add column ver_major INTEGER;
                alter table packages add column ver_minor INTEGER;
                alter table packages add column ver_patch INTEGER;
            )", "trying to add SEMVER columns to table \"packages2\"");
    }

    if (version == 7 || version == 8) {
        drop_table("pkg_info");
    }
    if (version == 13) {
        exec(R"(
                ALTER TABLE pkg_info ADD COLUMN author STRING;
                ALTER TABLE pkg_info ADD COLUMN topics STRING;
            )", "trying to add new fields to table \"pkg_info\"");
    }
    else {
        exec(R"(
                create table if not exists pkg_info (
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
                )
            )", "trying to create pkg_info table");
    }
    if (version == 8) {
        db_err = sqlite3_exec(db_handle, R"(
                create unique index if not exists pkg_info_pkg_id on pkg_info (pkg_id);
            )", nullptr, nullptr, &errmsg);
        if (db_err != 0) throw sqlite_error(db_handle, db_err, "trying to create index pkg_info_pkg_id on pkg_info table");
    }

    if (version == 10 || version == 11) {
        select("select id, version from packages2", [&](int col_count, const char* const col_values[], const char* const col_names[]) -> int {
            static const auto re = std::regex("^(\\d+)\\.(\\d+)\\.(\\d+)$");
            std::smatch m;
            std::string value = col_values[1];
            if (std::regex_match(value, m, re)) {
                auto major = std::stoi(m[1]), minor = std::stoi(m[2]), patch = std::stoi(m[3]);
                std::cout << "SEMVER: " << major << "." << minor << "." << patch << std::endl;
                // TODO: very poor example, use prepared statements and parameter binding!
                // TODO: could computed fields be used instead ?
                exec(fmt::format("update packages2 set ver_major={1}, ver_minor={2}, ver_patch={3} where id={0}", col_values[0], major, minor, patch).c_str(),
                    "trying to set SEMVER values in \"packages2\""
                );
            }
            return 0;
            });
    }

    exec("pragma user_version = 14;", "trying to set database version");

    stmt_upsert_package_description = [&]() {
        static const auto statement = R"(
                insert into pkg_info
                    (pkg_id, description, license, provides, author, topics, last_poll)
                    values(:PKG_ID, :DESCRIPTION, :LICENSE, :PROVIDES, :AUTHOR, :TOPICS, datetime('now'))
                on conflict (pkg_id) do update
                    set pkg_id=:PKG_ID, description=:DESCRIPTION, license=:LICENSE, author=:AUTHOR, topics=:TOPICS, last_poll=datetime('now');
            )";
        sqlite3_stmt* stmt = nullptr;
        auto err = sqlite3_prepare_v2(db_handle, statement, -1, &stmt, nullptr);
        if (err != SQLITE_OK) throw sqlite_error(db_handle, err, "trying to prepare statement upsert_package_description");
        return stmt;
    }();

#endif
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
