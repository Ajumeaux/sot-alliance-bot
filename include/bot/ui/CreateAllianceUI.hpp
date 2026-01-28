#pragma once

#include <memory>
#include <dpp/dpp.h>

#include "bot/ui/IModalUI.hpp"

namespace odb { namespace pgsql { class database; } }

static void create_or_update_alliance_roster_message(
    dpp::cluster* cluster,
    const std::shared_ptr<odb::pgsql::database>& db,
    std::uint64_t alliance_id,
    dpp::snowflake thread_id
);

class CreateAllianceUI : public IModalUI {
public:
    static void open_modal(const dpp::slashcommand_t& event);

    bool handle_modal(const dpp::form_submit_t& event,
                      const std::shared_ptr<odb::pgsql::database>& db) const override;

    static bool handle_select(const dpp::select_click_t& event,
                              const std::shared_ptr<odb::pgsql::database>& db);

    static bool handle_button(const dpp::button_click_t& event,
                              const std::shared_ptr<odb::pgsql::database>& db);
};
