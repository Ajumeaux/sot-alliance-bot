#include "db/Database.hpp"
#include "util/env.hpp"

#include <iostream>
#include <odb/pgsql/database.hxx>

DbConfig load_db_config_from_env() {
    DbConfig cfg;
    cfg.host = getenv_or("DB_HOST", "db");
    cfg.user = getenv_or("DB_USER", "botuser");
    cfg.password = getenv_or("DB_PASSWORD", "botpassword");
    cfg.name = getenv_or("DB_NAME", "botdb");

    try {
        cfg.port = static_cast<std::uint32_t>(
            std::stoul(getenv_or("DB_PORT", "5432"))
        );
    } catch (...) {
        std::cerr << "Warning : DB_PORT invalide, utilisation de 5432.\n";
        cfg.port = 5432;
    }

    return cfg;
}

std::shared_ptr<odb::pgsql::database> make_database(const DbConfig& cfg) {
    return std::make_shared<odb::pgsql::database>(
        cfg.user,
        cfg.password,
        cfg.name,
        cfg.host,
        cfg.port
    );
}
