#pragma once

#include <memory>
#include <dpp/dpp.h>

#include "bot/ui/IModalUI.hpp"

namespace odb { namespace pgsql { class database; } }

class EditAllianceUI : public IModalUI {
public:
    static void open(const dpp::slashcommand_t& event,
                     const std::shared_ptr<odb::pgsql::database>& db);

    static bool handle_button(const dpp::button_click_t& event,
                              const std::shared_ptr<odb::pgsql::database>& db);

    static bool handle_select(const dpp::select_click_t& event,
                              const std::shared_ptr<odb::pgsql::database>& db);

    bool handle_modal(const dpp::form_submit_t& event,
                      const std::shared_ptr<odb::pgsql::database>& db) const override;
};
