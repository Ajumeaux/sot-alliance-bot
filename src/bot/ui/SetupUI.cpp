#include "bot/ui/SetupUI.hpp"

#include <iostream>

#include <odb/pgsql/database.hxx>
#include <odb/transaction.hxx>
#include <odb/exceptions.hxx>

#include "model/bot_settings.hxx"
#include "bot_settings-odb.hxx"

namespace {
    static void ack_select(const dpp::select_click_t& event)
    {
        event.reply();
    }
}

bool SetupUI::handle_button(const dpp::button_click_t& event,
                            const std::shared_ptr<odb::pgsql::database>& /*db*/) const
{
    const std::string& id = event.custom_id;

    if (id == "setup_channels") {
        dpp::message m(event.command.channel_id, "Choisis les salons pour le bot :");
        m.set_flags(dpp::m_ephemeral);

        m.add_component(
            dpp::component().add_component(
                dpp::component()
                    .set_type(dpp::cot_channel_selectmenu)
                    .set_id("setup_channel_commands")
                    .set_placeholder("Salon pour les commandes du bot")
                    .set_min_values(1)
                    .set_max_values(1)
                    .add_channel_type(dpp::CHANNEL_TEXT)
            )
        );

        m.add_component(
            dpp::component().add_component(
                dpp::component()
                    .set_type(dpp::cot_channel_selectmenu)
                    .set_id("setup_channel_ping")
                    .set_placeholder("Salon pour les annonces/pings d'alliance")
                    .set_min_values(1)
                    .set_max_values(1)
                    .add_channel_type(dpp::CHANNEL_TEXT)
            )
        );

        m.add_component(
            dpp::component().add_component(
                dpp::component()
                    .set_type(dpp::cot_channel_selectmenu)
                    .set_id("setup_channel_alliance_forum")
                    .set_placeholder("Salon forum pour les alliances")
                    .set_min_values(1)
                    .set_max_values(1)
                    .add_channel_type(dpp::CHANNEL_FORUM)
            )
        );

        m.add_component(
            dpp::component().add_component(
                dpp::component()
                    .set_type(dpp::cot_channel_selectmenu)
                    .set_id("setup_channel_logs")
                    .set_placeholder("Salon logs du bot")
                    .set_min_values(1)
                    .set_max_values(1)
                    .add_channel_type(dpp::CHANNEL_TEXT)
            )
        );

        event.reply(m);
        return true;
    }
    else if (id == "setup_roles") {
        dpp::message m(event.command.channel_id, "Choisis les rôles utilisés par le bot :");
        m.set_flags(dpp::m_ephemeral);

        m.add_component(
            dpp::component().add_component(
                dpp::component()
                    .set_type(dpp::cot_role_selectmenu)
                    .set_id("setup_role_organizer")
                    .set_placeholder("Rôle organisateur d'alliance")
                    .set_min_values(1)
                    .set_max_values(1)
            )
        );

        m.add_component(
            dpp::component().add_component(
                dpp::component()
                    .set_type(dpp::cot_role_selectmenu)
                    .set_id("setup_role_notify")
                    .set_placeholder("Rôle ping alliance (notifications)")
                    .set_min_values(1)
                    .set_max_values(1)
            )
        );

        event.reply(m);
        return true;
    }
    else if (id == "setup_advanced") {
        dpp::interaction_modal_response modal("setup_advanced_modal",
                                              "Options avancées");

        modal.add_component(
            dpp::component()
                .set_label("Nombre de bateaux par défaut")
                .set_id("field_max_ships")
                .set_type(dpp::cot_text)
                .set_placeholder("6")
                .set_min_length(1)
                .set_max_length(2)
                .set_text_style(dpp::text_short)
        );

        modal.add_row();
        modal.add_component(
            dpp::component()
                .set_label("Timezone (ex: Europe/Paris)")
                .set_id("field_timezone")
                .set_type(dpp::cot_text)
                .set_placeholder("Europe/Paris")
                .set_min_length(3)
                .set_max_length(100)
                .set_text_style(dpp::text_short)
        );

        event.dialog(modal);
        return true;
    }

    return false;
}

bool SetupUI::handle_select(const dpp::select_click_t& event,
                            const std::shared_ptr<odb::pgsql::database>& db) const
{
    const std::string& id = event.custom_id;

    if (event.command.guild_id == 0)
        return false;

    const bool is_channel_select =
        (id == "setup_channel_commands" ||
         id == "setup_channel_ping" ||
         id == "setup_channel_alliance_forum" ||
         id == "setup_channel_logs");

    const bool is_role_select =
        (id == "setup_role_organizer" ||
         id == "setup_role_notify");

    if (!is_channel_select && !is_role_select) {
        return false;
    }

    if (event.values.empty()) {
        dpp::message msg("❌ Tu n'as rien sélectionné.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return true;
    }

    std::uint64_t selected_id = 0;
    try {
        selected_id = std::stoull(event.values[0]);
    } catch (...) {
        dpp::message msg("❌ Valeur de sélection invalide.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return true;
    }

    std::uint64_t guild_id = static_cast<std::uint64_t>(event.command.guild_id);

    try {
        odb::transaction t(db->begin());

        std::unique_ptr<BotSettings> settings;
        try {
            settings.reset(db->load<BotSettings>(guild_id));
        } catch (const odb::object_not_persistent&) {
            settings = std::make_unique<BotSettings>(guild_id);
            db->persist(*settings);
        }

        if (id == "setup_channel_commands") {
            settings->command_channel_id(selected_id);
        } else if (id == "setup_channel_ping") {
            settings->ping_channel_id(selected_id);
        } else if (id == "setup_channel_alliance_forum") {
            settings->alliance_forum_channel_id(selected_id);
        } else if (id == "setup_channel_logs") {
            settings->log_channel_id(selected_id);
        } else if (id == "setup_role_organizer") {
            settings->organizer_role_id(selected_id);
        } else if (id == "setup_role_notify") {
            settings->notify_role_id(selected_id);
        }

        db->update(*settings);
        t.commit();

        const bool all_channels_set =
            settings->command_channel_id() != 0 &&
            settings->ping_channel_id() != 0 &&
            settings->alliance_forum_channel_id() != 0 &&
            settings->log_channel_id() != 0;

        const bool all_roles_set =
            settings->organizer_role_id() != 0 &&
            settings->notify_role_id() != 0;

        if (all_channels_set && all_roles_set) {
            dpp::message msg("✅ Configuration complète pour ce serveur !");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
        } else {
            ack_select(event);
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "[SetupUI] Erreur DB dans handle_select : " << ex.what() << "\n";
        dpp::message msg(std::string("❌ Erreur DB : ") + ex.what());
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
    }

    return true;
}

bool SetupUI::handle_modal(const dpp::form_submit_t& event,
                           const std::shared_ptr<odb::pgsql::database>& db) const
{
    if (event.custom_id != "setup_advanced_modal")
        return false;

    if (event.command.guild_id == 0)
        return false;

    std::uint64_t guild_id = static_cast<std::uint64_t>(event.command.guild_id);

    int max_ships_int = get_int_field(event, 0, 0, 6);
    std::string timezone_str = get_text_field(event, 1, 0);

    if (max_ships_int < 1) max_ships_int = 1;
    if (max_ships_int > 20) max_ships_int = 20;

    try {
        odb::transaction t(db->begin());

        std::unique_ptr<BotSettings> settings;
        try {
            settings.reset(db->load<BotSettings>(guild_id));
        } catch (const odb::object_not_persistent&) {
            settings = std::make_unique<BotSettings>(guild_id);
            db->persist(*settings);
        }

        settings->default_max_ships(static_cast<unsigned short>(max_ships_int));
        if (!timezone_str.empty())
            settings->timezone(timezone_str);

        db->update(*settings);
        t.commit();

        reply_ephemeral(event, "✅ Options avancées mises à jour !");
    }
    catch (const std::exception& ex) {
        std::cerr << "[SetupUI] Erreur DB dans handle_modal : " << ex.what() << "\n";
        reply_ephemeral(event, std::string("❌ Erreur DB : ") + ex.what());
    }

    return true;
}
