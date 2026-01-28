#pragma once

#include <memory>

namespace odb { namespace pgsql {
    class database;
}}

void init_schema(const std::shared_ptr<odb::pgsql::database>& db);
void test_connection(const std::shared_ptr<odb::pgsql::database>& db);
