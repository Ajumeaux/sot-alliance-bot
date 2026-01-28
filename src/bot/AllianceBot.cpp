#include "bot/AllianceBot.hpp"

#include <iostream>

#include "bot/commands/SetupCommand.hpp"
#include "bot/commands/CreateAllianceCommand.hpp"
#include "bot/commands/CancelAllianceCommand.hpp"
#include "bot/commands/JoinAllianceCommand.hpp"
#include "bot/commands/LeaveAllianceCommand.hpp"
#include "bot/commands/StartAllianceCommand.hpp"
#include "bot/commands/EndAllianceCommand.hpp"
#include "bot/commands/EditAllianceCommand.hpp"

#include "bot/ui/SetupUI.hpp"
#include "bot/ui/CreateAllianceUI.hpp"
#include "bot/ui/CancelAllianceUI.hpp"
#include "bot/ui/JoinAllianceUI.hpp"
#include "bot/ui/LeaveAllianceUI.hpp"
#include "bot/ui/EditAllianceUI.hpp"
#include "bot/ui/EndAllianceUI.hpp"

AllianceBot::AllianceBot(const std::string& token,
                         std::shared_ptr<odb::pgsql::database> db)
    : bot_(token),
      db_(std::move(db))
{
    bot_.on_log(dpp::utility::cout_logger());

    init_commands();
    init_modals();
    register_event_handlers();
}

void AllianceBot::run() {
    bot_.start(dpp::st_wait);
}

void AllianceBot::init_commands() {
    commands_.emplace("setup",  std::make_unique<SetupCommand>());
    commands_.emplace("creer", std::make_unique<CreateAllianceCommand>());
    commands_.emplace("annuler", std::make_unique<CancelAllianceCommand>());
    commands_.emplace("rejoindre",   std::make_unique<JoinAllianceCommand>());
    commands_.emplace("quitter",  std::make_unique<LeaveAllianceCommand>());
    commands_.emplace("demarrer",  std::make_unique<StartAllianceCommand>());
    commands_.emplace("terminer",    std::make_unique<EndAllianceCommand>());
    commands_.emplace("modifier",   std::make_unique<EditAllianceCommand>());

    bot_.on_ready([this](const dpp::ready_t& event) {
        if (!dpp::run_once<struct register_commands>()) {
            return;
        }

        bot_.global_commands_get([this](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                std::cerr << "[CMD] global_commands_get error: "
                          << cb.get_error().message << "\n";
                return;
            }

            auto cmds = cb.get<dpp::slashcommand_map>();

            for (auto& [id, cmd] : cmds) {
                const std::string& name = cmd.name;

                if (name != "alliance") {
                    std::cerr << "[CMD] Suppression de l'ancienne commande globale: /"
                              << name << "\n";

                    bot_.global_command_delete(
                        id,
                        [](const dpp::confirmation_callback_t& cb2) {
                            if (cb2.is_error()) {
                                std::cerr << "[CMD] global_command_delete error: "
                                          << cb2.get_error().message << "\n";
                            }
                        }
                    );
                }
            }

            dpp::slashcommand alliance_cmd;
            alliance_cmd.set_name("alliance")
                        .set_description("Gestion des alliances Sea of Thieves")
                        .set_application_id(bot_.me.id);

            for (auto& [name, cmd_ptr] : commands_) {
                dpp::command_option opt;
                cmd_ptr->build_subcommand(opt); // type, name, description
                alliance_cmd.add_option(opt);
            }

            bot_.global_command_create(
                alliance_cmd,
                [](const dpp::confirmation_callback_t& cb2) {
                    if (cb2.is_error()) {
                        std::cerr << "[CMD] global_command_create(/alliance) error: "
                                  << cb2.get_error().message << "\n";
                    } else {
                        std::cout << "[CMD] Commande /alliance enregistrée.\n";
                    }
                }
            );
        });
    });
}



void AllianceBot::init_modals() {
    {
        auto ui = std::make_unique<SetupUI>();
        setup_ui_ = ui.get();
        modal_handlers_.emplace("setup_advanced_modal", std::move(ui));
    }

    {
        auto ui = std::make_unique<CreateAllianceUI>();
        modal_handlers_.emplace("create_alliance_datetime_modal", std::move(ui));
    }

    {
        auto ui = std::make_unique<CreateAllianceUI>();
        modal_handlers_.emplace("create_alliance_ship_role_custom_modal", std::move(ui));
    }

    {
        auto ui = std::make_unique<EditAllianceUI>();
        modal_handlers_.emplace("edit_alliance_sale_modal", std::move(ui));
    }
}

void AllianceBot::register_event_handlers() {
    bot_.on_slashcommand([this](const dpp::slashcommand_t& event) {
        const auto& cmd_data = std::get<dpp::command_interaction>(event.command.data);

        const std::string root_name = cmd_data.name;
        if (root_name != "alliance") {
            return;
        }

        if (cmd_data.options.empty()) {
            dpp::message msg("Sous-commande manquante 🤔\nEx : `/alliance create`");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }
        const auto& sub = cmd_data.options[0];
        const std::string sub_name = sub.name; // "create", "join", ...

        auto it = commands_.find(sub_name);
        if (it == commands_.end()) {
            dpp::message msg("Sous-commande inconnue 🤔");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        try {
            it->second->handle(event, db_);
        } catch (const std::exception& ex) {
            std::cerr << "[CMD] Exception dans '/alliance " << sub_name << "': "
                      << ex.what() << "\n";
            dpp::message msg("Erreur interne lors de l'exécution de la commande ❌");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
        }
    });

    bot_.on_button_click([this](const dpp::button_click_t& event) {
        if (setup_ui_ && setup_ui_->handle_button(event, db_)) {
            return;
        }
        if (CreateAllianceUI::handle_button(event, db_)) {
            return;
        }
        if (EditAllianceUI::handle_button(event, db_)) {
            return;
        }
        if (EndAllianceUI::handle_button(event, db_)) {
            return;
        }
        if (LeaveAllianceUI::handle_button(event, db_)) {
            return;
        }
        if (CancelAllianceUI::handle_button(event, db_)) {
            return;
        }
    });

    bot_.on_select_click([this](const dpp::select_click_t& event) {
        if (setup_ui_ && setup_ui_->handle_select(event, db_)) {
            return;
        }
        if (CreateAllianceUI::handle_select(event, db_)) {
            return;
        }
        if (JoinAllianceUI::handle_select(event, db_)) {
            return;
        }
        if (EditAllianceUI::handle_select(event, db_)) {
            return;
        }
    });

    bot_.on_form_submit([this](const dpp::form_submit_t& event) {
        const std::string& id = event.custom_id;

        auto it = modal_handlers_.find(id);
        if (it == modal_handlers_.end()) {
            return;
        }

        if (it->second->handle_modal(event, db_)) {
            return;
        }
    });
}
