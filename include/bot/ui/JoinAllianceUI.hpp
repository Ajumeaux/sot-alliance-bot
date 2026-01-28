#pragma once

#include <memory>

namespace dpp {
    class slashcommand_t;
    class select_click_t;
}

namespace odb { namespace pgsql {
    class database;
}}

class JoinAllianceUI {
public:
    static void open(const dpp::slashcommand_t& event,
                     const std::shared_ptr<odb::pgsql::database>& db);

    static bool handle_select(const dpp::select_click_t& event,
                              const std::shared_ptr<odb::pgsql::database>& db);
};
