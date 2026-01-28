#pragma once

#include <memory>
#include <string>
#include <cstdint>

namespace odb { namespace pgsql {
    class database;
}}

struct DbConfig {
    std::string host;
    std::string user;
    std::string password;
    std::string name;
    std::uint32_t port;
};

DbConfig load_db_config_from_env();

std::shared_ptr<odb::pgsql::database> make_database(const DbConfig& cfg);
