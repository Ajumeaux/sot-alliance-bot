#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <dpp/dpp.h>

#include "bot/commands/ISlashCommand.hpp"
#include "bot/ui/IModalUI.hpp"

namespace odb { namespace pgsql {
    class database;
}}

class SetupUI;

class AllianceBot {
public:
    AllianceBot(const std::string& token,
                std::shared_ptr<odb::pgsql::database> db);

    void run();

private:
    dpp::cluster bot_;
    std::shared_ptr<odb::pgsql::database> db_;

    // "ping" -> PingCommand, "setup" -> SetupCommand, ...
    std::unordered_map<std::string, std::unique_ptr<ISlashCommand>> commands_;

    // "setup_advanced_modal" -> SetupUI, "create_alliance_modal" -> CreateAllianceUI, ...
    std::unordered_map<std::string, std::unique_ptr<IModalUI>> modal_handlers_;

    SetupUI* setup_ui_ = nullptr;

    void init_commands();
    void init_modals();
    void register_event_handlers();
};
