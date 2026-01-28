#include "bot/ui/EditAllianceUI.hpp"

#include <sstream>
#include <iomanip>
#include <iostream>
#include <cctype>
#include <memory>
#include <ctime>

#include <dpp/dpp.h>

#include <odb/transaction.hxx>
#include <odb/query.hxx>
#include <odb/exceptions.hxx>

#include "model/alliances.hxx"
#include "alliances-odb.hxx"

#include "model/ships.hxx"
#include "ships-odb.hxx"

#include "bot/AllianceHelpers.hpp"

namespace {

using alliance_helpers::trim;
using alliance_helpers::parse_time_to_hhmm;
using alliance_helpers::parse_date_ddmm;

static std::uint64_t parse_mention_id(const std::string& mention) {
    std::string digits;
    digits.reserve(mention.size());
    for (char c : mention) {
        if (c >= '0' && c <= '9')
            digits.push_back(c);
    }
    if (digits.empty())
        return 0;

    try {
        return std::stoull(digits);
    } catch (...) {
        return 0;
    }
}

static void ack_select(const dpp::select_click_t& event)
{
    event.reply();
}

static bool set_ship_hull_from_value(Ship& ship, const std::string& value)
{
    if (value == "sloop") {
        ship.hull_type(HullType::sloop);
    } else if (value == "brig") {
        ship.hull_type(HullType::brig);
    } else if (value == "galleon") {
        ship.hull_type(HullType::galleon);
    } else {
        return false;
    }
    return true;
}

} // namespace

void EditAllianceUI::open(const dpp::slashcommand_t& event,
                          const std::shared_ptr<odb::pgsql::database>& db)
{
    const std::uint64_t guild_id   = static_cast<std::uint64_t>(event.command.guild_id);
    const std::uint64_t channel_id = static_cast<std::uint64_t>(event.command.channel_id);
    const std::uint64_t user_id    = static_cast<std::uint64_t>(event.command.usr.id);

    if (guild_id == 0) {
        dpp::message msg("❌ Cette commande ne peut pas être utilisée en messages privés.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    try {
        using AllianceQuery  = odb::query<Alliance>;
        using AllianceResult = odb::result<Alliance>;

        odb::transaction t(db->begin());

        AllianceResult ares(
            db->query<Alliance>(
                AllianceQuery::guild_id == guild_id &&
                AllianceQuery::thread_channel_id == channel_id
            )
        );

        if (ares.empty()) {
            t.commit();
            dpp::message msg(
                "❌ Ce thread n'est plus associé à une alliance.\n"
                "La commande `/alliance edit` doit être utilisée **dans un post d'alliance créé par le bot**."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        Alliance alliance = *ares.begin();
        t.commit();

        const std::uint64_t organizer_id = alliance.organizer_id();
        const std::uint64_t right_hand_id =
            alliance.right_hand().empty() ? 0 : parse_mention_id(alliance.right_hand());

        if (user_id != organizer_id && user_id != right_hand_id) {
            dpp::message msg(
                "❌ Tu n'es **ni l'organisateur** ni **le bras droit** de cette alliance."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        if (alliance.status() == AllianceStatus::finished ||
            alliance.status() == AllianceStatus::cancelled)
        {
            dpp::message msg(
                "❌ Cette alliance est **terminée** ou **annulée**, tu ne peux plus la modifier."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        const std::time_t scheduled_at = alliance.scheduled_at();
        const std::time_t sale_at      = alliance.sale_at();
        const bool reprise             = alliance.ships_reuse_planned();

        const std::string start_str = alliance_helpers::format_hhmm(scheduled_at);
        const std::string sale_str  = alliance_helpers::format_hhmm(sale_at);

        const std::string start_ts = "<t:" + std::to_string(scheduled_at) + ":t>";
        const std::string sale_ts  = "<t:" + std::to_string(sale_at) + ":t>";

        std::ostringstream content;
        content << "✏️ **Édition de l'alliance** : **" << alliance.name() << "**\n\n"
                << "• Début actuel : " << start_ts << " (" << start_str << ")\n"
                << "• Vente actuelle : " << sale_ts  << " (" << sale_str  << ")\n"
                << "• Reprise des bateaux : " << (reprise ? "✅ Prévu" : "❌ Non prévu") << "\n\n"
                << "**Actions disponibles :**\n"
                << "> 🕒 Modifier la date ou les heures\n"
                << "> 🚢 Modifier la flotte\n"
                << "> 🔁 Modifier la reprise\n";

        dpp::message msg;
        msg.set_flags(dpp::m_ephemeral);
        msg.set_content(content.str());

        dpp::component row1;
        row1.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_id("edit_alliance_schedule_button")
                .set_label("Éditer date & heures")
                .set_style(dpp::cos_primary)
        );
        row1.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_id("edit_alliance_fleet_button")
                .set_label("Éditer la flotte")
                .set_style(dpp::cos_secondary)
        );
        msg.add_component(row1);

        dpp::component reuse_select;
        reuse_select.set_type(dpp::cot_selectmenu)
                    .set_id("edit_alliance_reuse")
                    .set_min_values(1)
                    .set_max_values(1)
                    .set_placeholder(
                        reprise ?
                        "Reprise actuelle : prévue" :
                        "Reprise actuelle : non prévue"
                    );

        reuse_select.add_select_option(dpp::select_option("Reprise prévue", "yes"));
        reuse_select.add_select_option(dpp::select_option("Pas de reprise", "no"));

        msg.add_component(
            dpp::component().add_component(reuse_select)
        );

        event.reply(msg);
    }
    catch (const std::exception& ex) {
        std::cerr << "[EditAllianceUI::open] Exception : " << ex.what() << "\n";
        dpp::message msg("❌ Erreur interne lors de l'ouverture de l'éditeur.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
    }
}


bool EditAllianceUI::handle_button(const dpp::button_click_t& event,
                                   const std::shared_ptr<odb::pgsql::database>& db)
{
    if (event.command.guild_id == 0)
        return false;

    const std::string& id = event.custom_id;

    if (id == "edit_alliance_schedule_button") {
        dpp::interaction_modal_response modal(
            "edit_alliance_schedule_modal",
            "Éditer date & heures"
        );

        modal.add_component(
            dpp::component()
                .set_label("Date (ex: 24/11 ou 24/11/2025)")
                .set_id("field_date_edit")
                .set_type(dpp::cot_text)
                .set_placeholder("24/11")
                .set_min_length(0)
                .set_max_length(20)
                .set_text_style(dpp::text_short)
        );

        modal.add_row();
        modal.add_component(
            dpp::component()
                .set_label("Heure de début (ex: 7h, 7h30 ou 07:30)")
                .set_id("field_start_time_edit")
                .set_type(dpp::cot_text)
                .set_placeholder("07:30")
                .set_min_length(0)
                .set_max_length(10)
                .set_text_style(dpp::text_short)
        );

        modal.add_row();
        modal.add_component(
            dpp::component()
                .set_label("Heure de vente (ex: 18h, 18h00 ou 18:00)")
                .set_id("field_sale_time_edit")
                .set_type(dpp::cot_text)
                .set_placeholder("18:00")
                .set_min_length(0)
                .set_max_length(10)
                .set_text_style(dpp::text_short)
        );

        event.dialog(modal);
        return true;
    }

    if (id == "edit_alliance_fleet_button") {
        dpp::snowflake guild_id   = event.command.guild_id;
        dpp::snowflake channel_id = event.command.channel_id;

        std::uint64_t guild_id_u64   = static_cast<std::uint64_t>(guild_id);
        std::uint64_t channel_id_u64 = static_cast<std::uint64_t>(channel_id);

        try {
            using AllianceQuery  = odb::query<Alliance>;
            using AllianceResult = odb::result<Alliance>;
            using ShipQuery      = odb::query<Ship>;
            using ShipResult     = odb::result<Ship>;

            odb::transaction t(db->begin());

            AllianceResult ares(
                db->query<Alliance>(
                    AllianceQuery::guild_id == guild_id_u64 &&
                    AllianceQuery::thread_channel_id == channel_id_u64
                )
            );

            auto ait = ares.begin();
            if (ait == ares.end()) {
                t.commit();
                dpp::message msg("❌ Ce thread n'est plus associé à une alliance connue.");
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            Alliance alliance = *ait;
            std::uint64_t alliance_id = alliance.id();

            ShipQuery sq(ShipQuery::alliance_id == alliance_id);
            sq += " ORDER BY " + ShipQuery::slot;

            ShipResult sres(db->query<Ship>(sq));

            std::vector<Ship> ships;
            for (const Ship& s : sres) {
                ships.push_back(s);
            }

            t.commit();

            if (ships.empty()) {
                dpp::message msg(
                    "❌ Aucun bateau n'est configuré pour cette alliance.\n"
                    "Tu peux d'abord configurer la flotte via `/create`."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            dpp::message m;
            m.set_flags(dpp::m_ephemeral);

            std::ostringstream content;
            content << "🛠️ Édition de la flotte pour **" << alliance.name() << "**\n"
                    << "Choisis le **bateau** à modifier :";
            m.set_content(content.str());

            dpp::component ship_select;
            ship_select.set_type(dpp::cot_selectmenu)
                       .set_id("edit_alliance_choose_ship")
                       .set_placeholder("Choisir un bateau")
                       .set_min_values(1)
                       .set_max_values(1);

            std::size_t idx = 0;
            for (const Ship& ship : ships) {
                std::string hull_label = alliance_helpers::hull_label(ship.hull_type());
                std::string role = ship.crew_role().empty() ? "Libre" : ship.crew_role();

                std::ostringstream label;
                label << hull_label << " - " << role << " (#" << (idx + 1) << ")";

                ship_select.add_select_option(
                    dpp::select_option(label.str(), std::to_string(ship.id()))
                );
                ++idx;
            }

            dpp::component row;
            row.add_component(ship_select);
            m.add_component(row);

            event.reply(m);
            return true;
        }
        catch (const std::exception& ex) {
            std::cerr << "[EditAllianceUI::handle_button] Erreur DB (fleet) : "
                      << ex.what() << "\n";
            dpp::message msg("❌ Erreur interne lors du chargement de la flotte.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }
    }

    return false;
}

bool EditAllianceUI::handle_select(const dpp::select_click_t& event,
                                   const std::shared_ptr<odb::pgsql::database>& db)
{
    if (event.command.guild_id == 0)
        return false;

    const std::string& id = event.custom_id;
    if (event.values.empty()) {
        ack_select(event);
        return true;
    }

    dpp::snowflake guild_id   = event.command.guild_id;
    dpp::snowflake channel_id = event.command.channel_id;
    std::uint64_t guild_id_u64   = static_cast<std::uint64_t>(guild_id);
    std::uint64_t channel_id_u64 = static_cast<std::uint64_t>(channel_id);

    if (id == "edit_alliance_choose_ship") {
        const std::string& ship_id_str = event.values[0];
        std::uint64_t ship_id = 0;
        try {
            ship_id = std::stoull(ship_id_str);
        } catch (...) {
            ack_select(event);
            return true;
        }

        try {
            using AllianceQuery  = odb::query<Alliance>;
            using AllianceResult = odb::result<Alliance>;
            using ShipQuery      = odb::query<Ship>;
            using ShipResult     = odb::result<Ship>;

            odb::transaction t(db->begin());

            AllianceResult ares(
                db->query<Alliance>(
                    AllianceQuery::guild_id == guild_id_u64 &&
                    AllianceQuery::thread_channel_id == channel_id_u64
                )
            );

            auto ait = ares.begin();
            if (ait == ares.end()) {
                t.commit();
                dpp::message msg("❌ Ce thread n'est plus associé à une alliance connue.");
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            Alliance alliance = *ait;
            std::uint64_t alliance_id = alliance.id();

            ShipQuery sq(ShipQuery::alliance_id == alliance_id);
            sq += " ORDER BY " + ShipQuery::slot;
            ShipResult sres(db->query<Ship>(sq));

            std::vector<Ship> ships;
            for (const Ship& s : sres) {
                ships.push_back(s);
            }

            Ship* target = nullptr;
            std::size_t index = 0;
            for (std::size_t i = 0; i < ships.size(); ++i) {
                if (ships[i].id() == ship_id) {
                    target = &ships[i];
                    index = i;
                    break;
                }
            }

            if (!target) {
                t.commit();
                dpp::message msg("❌ Ce bateau n'existe plus pour cette alliance.");
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            t.commit();

            dpp::message m;
            m.set_flags(dpp::m_ephemeral);

            std::string hull_label = alliance_helpers::hull_label(target->hull_type());
            std::string role = target->crew_role().empty() ? "Libre" : target->crew_role();

            std::ostringstream content;
            content << "⚓ Édition du **bateau " << (index + 1) << "/" << ships.size()
                    << "** : " << hull_label << " - " << role << "\n"
                    << "Choisis une nouvelle **coque** et/ou un **rôle** pour ce navire.\n"
                    << "Tu peux choisir `Autre…` pour définir un rôle personnalisé.";
            m.set_content(content.str());

            std::string hull_select_id = "edit_alliance_ship_hull_" + std::to_string(ship_id);
            dpp::component hull_select;
            hull_select.set_type(dpp::cot_selectmenu)
                       .set_id(hull_select_id)
                       .set_placeholder("Type de navire")
                       .set_min_values(1)
                       .set_max_values(1);

            auto add_hull_opt = [&](const std::string& name, const std::string& value) {
                dpp::select_option opt(name, value);
                if (name == hull_label) {
                    opt.set_default(true);
                }
                hull_select.add_select_option(opt);
            };

            add_hull_opt("Sloop",     "sloop");
            add_hull_opt("Brigantin", "brig");
            add_hull_opt("Galion",    "galleon");

            m.add_component(dpp::component().add_component(hull_select));

            std::string role_select_id = "edit_alliance_ship_role_" + std::to_string(ship_id);
            dpp::component role_select;
            role_select.set_type(dpp::cot_selectmenu)
                       .set_id(role_select_id)
                       .set_placeholder("Rôle du navire")
                       .set_min_values(1)
                       .set_max_values(1);

            auto add_role_opt = [&](const std::string& name, const std::string& value) {
                dpp::select_option opt(name, value);
                if (name == role) {
                    opt.set_default(true);
                }
                role_select.add_select_option(opt);
            };

            add_role_opt("FDD",      "FDD");
            add_role_opt("Event",    "Event");
            add_role_opt("Athéna",   "Athéna");
            add_role_opt("Chasseur","Chasseur");
            add_role_opt("Libre",    "Libre");

            dpp::select_option opt_custom("Autre…", "custom");
            if (role != "FDD" && role != "Event" && role != "Athéna" &&
                role != "Chasseur" && role != "Libre")
            {
                opt_custom.set_default(true);
            }
            role_select.add_select_option(opt_custom);

            m.add_component(dpp::component().add_component(role_select));

            event.reply(m);
        }
        catch (const std::exception& ex) {
            std::cerr << "[EditAllianceUI::handle_select] Erreur DB (choose_ship) : "
                      << ex.what() << "\n";
            dpp::message msg("❌ Erreur interne lors du chargement du bateau.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
        }

        return true;
    }

    if (id == "edit_alliance_reuse") {
        const std::string& value = event.values[0];
        bool reprise = (value == "yes");

        try {
            using AllianceQuery  = odb::query<Alliance>;
            using AllianceResult = odb::result<Alliance>;

            odb::transaction t(db->begin());

            AllianceResult ares(
                db->query<Alliance>(
                    AllianceQuery::guild_id == guild_id_u64 &&
                    AllianceQuery::thread_channel_id == channel_id_u64
                )
            );

            auto ait = ares.begin();
            if (ait == ares.end()) {
                t.commit();
                ack_select(event);
                return true;
            }

            Alliance alliance = *ait;
            alliance.ships_reuse_planned(reprise);
            db->update(alliance);
            std::uint64_t alliance_id = alliance.id();
            std::uint64_t thread_id   = alliance.thread_channel_id();
            t.commit();

            dpp::cluster* cluster = event.from()->creator;
            if (cluster) {
                alliance_helpers::create_or_update_alliance_roster_message(
                    cluster,
                    db,
                    alliance_id,
                    static_cast<dpp::snowflake>(thread_id)
                );
            }
        }
        catch (const std::exception& ex) {
            std::cerr << "[EditAllianceUI::handle_select] Erreur DB (reprise) : "
                      << ex.what() << "\n";
        }

        ack_select(event);
        return true;
    }

    if (id.rfind("edit_alliance_ship_hull_", 0) == 0) {
        const std::string& value = event.values[0];
        std::string ship_id_str = id.substr(std::string("edit_alliance_ship_hull_").size());

        std::uint64_t ship_id = 0;
        try {
            ship_id = std::stoull(ship_id_str);
        } catch (...) {
            ack_select(event);
            return true;
        }

        try {
            odb::transaction t(db->begin());

            std::unique_ptr<Ship> ship(db->load<Ship>(ship_id));
            if (!ship) {
                t.commit();
                ack_select(event);
                return true;
            }

            if (!set_ship_hull_from_value(*ship, value)) {
                t.commit();
                ack_select(event);
                return true;
            }

            std::uint64_t alliance_id = ship->alliance_id();
            db->update(*ship);
            t.commit();

            dpp::cluster* cluster = event.from()->creator;
            if (cluster) {
                try {
                    odb::transaction t2(db->begin());
                    std::unique_ptr<Alliance> a(
                        db->load<Alliance>(alliance_id)
                    );
                    dpp::snowflake thread_id = static_cast<dpp::snowflake>(a->thread_channel_id());
                    t2.commit();

                    alliance_helpers::create_or_update_alliance_roster_message(
                        cluster,
                        db,
                        alliance_id,
                        thread_id
                    );
                } catch (...) {
                }
            }
        }
        catch (const std::exception& ex) {
            std::cerr << "[EditAllianceUI::handle_select] Erreur DB (ship hull) : "
                      << ex.what() << "\n";
        }

        ack_select(event);
        return true;
    }

    if (id.rfind("edit_alliance_ship_role_", 0) == 0) {
        const std::string& value = event.values[0];
        std::string ship_id_str = id.substr(std::string("edit_alliance_ship_role_").size());

        std::uint64_t ship_id = 0;
        try {
            ship_id = std::stoull(ship_id_str);
        } catch (...) {
            ack_select(event);
            return true;
        }

        if (value == "custom") {
            dpp::interaction_modal_response modal(
                "edit_alliance_custom_ship_role_" + std::to_string(ship_id),
                "Rôle personnalisé du navire"
            );

            modal.add_component(
                dpp::component()
                    .set_label("Nom du rôle (ex: FDD Reaper, Bilge Rat...)")
                    .set_id("field_custom_ship_role")
                    .set_type(dpp::cot_text)
                    .set_placeholder("Nom du rôle")
                    .set_min_length(1)
                    .set_max_length(50)
                    .set_text_style(dpp::text_short)
            );

            event.dialog(modal);
            return true;
        }

        std::string new_role;
        if (value == "FDD") {
            new_role = "FDD";
        } else if (value == "Event") {
            new_role = "Event";
        } else if (value == "Athéna") {
            new_role = "Athéna";
        } else if (value == "Chasseur") {
            new_role = "Chasseur";
        } else if (value == "Libre") {
            new_role = "Libre";
        } else {
            ack_select(event);
            return true;
        }

        try {
            odb::transaction t(db->begin());

            std::unique_ptr<Ship> ship(db->load<Ship>(ship_id));
            if (!ship) {
                t.commit();
                ack_select(event);
                return true;
            }

            ship->crew_role(new_role);
            std::uint64_t alliance_id = ship->alliance_id();
            db->update(*ship);
            t.commit();

            dpp::cluster* cluster = event.from()->creator;
            if (cluster) {
                try {
                    odb::transaction t2(db->begin());
                    std::unique_ptr<Alliance> a(
                        db->load<Alliance>(alliance_id)
                    );
                    dpp::snowflake thread_id = static_cast<dpp::snowflake>(a->thread_channel_id());
                    t2.commit();

                    alliance_helpers::create_or_update_alliance_roster_message(
                        cluster,
                        db,
                        alliance_id,
                        thread_id
                    );
                } catch (...) {
                }
            }
        }
        catch (const std::exception& ex) {
            std::cerr << "[EditAllianceUI::handle_select] Erreur DB (ship role) : "
                      << ex.what() << "\n";
        }

        ack_select(event);
        return true;
    }

    return false;
}

bool EditAllianceUI::handle_modal(const dpp::form_submit_t& event,
                                  const std::shared_ptr<odb::pgsql::database>& db) const
{
    if (event.command.guild_id == 0)
        return false;

    const std::string& cid = event.custom_id;

    if (cid == "edit_alliance_schedule_modal") {
        dpp::snowflake guild_id   = event.command.guild_id;
        dpp::snowflake channel_id = event.command.channel_id;

        std::string date_input  = trim(get_text_field(event, 0, 0));
        std::string start_input = trim(get_text_field(event, 1, 0));
        std::string sale_input  = trim(get_text_field(event, 2, 0));

        if (date_input.empty() && start_input.empty() && sale_input.empty()) {
            dpp::message msg("ℹ️ Aucun champ rempli, l'alliance n'a pas été modifiée.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        std::uint64_t guild_id_u64   = static_cast<std::uint64_t>(guild_id);
        std::uint64_t channel_id_u64 = static_cast<std::uint64_t>(channel_id);

        Alliance alliance;
        bool found = false;

        try {
            using AllianceQuery  = odb::query<Alliance>;
            using AllianceResult = odb::result<Alliance>;

            odb::transaction t(db->begin());

            AllianceResult ares(
                db->query<Alliance>(
                    AllianceQuery::guild_id == guild_id_u64 &&
                    AllianceQuery::thread_channel_id == channel_id_u64
                )
            );

            auto ait = ares.begin();
            if (ait == ares.end()) {
                t.commit();
                dpp::message msg("❌ Ce thread n'est plus associé à une alliance connue.");
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            alliance = *ait;
            found = true;
            t.commit();
        }
        catch (const std::exception& ex) {
            std::cerr << "[EditAllianceUI::handle_modal] Erreur DB (chargement alliance) : "
                      << ex.what() << "\n";
            dpp::message msg("❌ Erreur interne lors du chargement de l'alliance.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        if (!found) {
            dpp::message msg("❌ Ce thread n'est plus associé à une alliance connue.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        std::time_t old_scheduled_at = alliance.scheduled_at();
        std::time_t old_sale_at      = alliance.sale_at();

        std::tm tm_base {};
#ifdef _WIN32
        localtime_s(&tm_base, &old_scheduled_at);
#else
        tm_base = *std::localtime(&old_scheduled_at);
#endif

        std::tm tm_start = tm_base;

        if (!date_input.empty()) {
            std::tm tmp {};
            if (!parse_date_ddmm(date_input, tm_base, tmp)) {
                dpp::message msg(
                    "❌ Je n'ai pas compris la date. Essaie par exemple `24/11` ou `24/11/2025`."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }
            tm_start = tmp;
        }

        if (!start_input.empty()) {
            std::string start_hhmm;
            if (!parse_time_to_hhmm(start_input, start_hhmm)) {
                dpp::message msg(
                    "❌ Je n'ai pas compris l'heure de début. Essaie par exemple `7h`, `7h30` ou `07:30`."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            auto pos = start_hhmm.find(':');
            int h = 0, m = 0;
            try {
                h = std::stoi(start_hhmm.substr(0, pos));
                m = std::stoi(start_hhmm.substr(pos + 1));
            } catch (...) {
                dpp::message msg("❌ Impossible d'interpréter l'heure de début.");
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            tm_start.tm_hour = h;
            tm_start.tm_min  = m;
            tm_start.tm_sec  = 0;
        }

        std::time_t new_scheduled_at = std::mktime(&tm_start);

        std::tm tm_sale {};
#ifdef _WIN32
        localtime_s(&tm_sale, &old_sale_at);
#else
        tm_sale = *std::localtime(&old_sale_at);
#endif

        tm_sale.tm_year = tm_start.tm_year;
        tm_sale.tm_mon  = tm_start.tm_mon;
        tm_sale.tm_mday = tm_start.tm_mday;

        if (!sale_input.empty()) {
            std::string sale_hhmm;
            if (!parse_time_to_hhmm(sale_input, sale_hhmm)) {
                dpp::message msg(
                    "❌ Je n'ai pas compris l'heure de vente. Essaie par exemple `18h`, `18h00` ou `18:00`."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            auto pos = sale_hhmm.find(':');
            int h = 0, m = 0;
            try {
                h = std::stoi(sale_hhmm.substr(0, pos));
                m = std::stoi(sale_hhmm.substr(pos + 1));
            } catch (...) {
                dpp::message msg("❌ Impossible d'interpréter l'heure de vente.");
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            tm_sale.tm_hour = h;
            tm_sale.tm_min  = m;
            tm_sale.tm_sec  = 0;
        }

        std::time_t new_sale_at = std::mktime(&tm_sale);

        if (new_sale_at <= new_scheduled_at) {
            dpp::message msg(
                "❌ L'heure de vente doit être **après** l'heure de début."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        alliance.scheduled_at(new_scheduled_at);
        alliance.sale_at(new_sale_at);

        try {
            odb::transaction t2(db->begin());
            db->update(alliance);
            t2.commit();
        }
        catch (const std::exception& ex) {
            std::cerr << "[EditAllianceUI::handle_modal] Erreur DB (update schedule) : "
                      << ex.what() << "\n";
            dpp::message msg("❌ Erreur DB lors de la mise à jour de la date/heure.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        dpp::cluster* cluster = event.from()->creator;
        if (cluster) {
            alliance_helpers::create_or_update_alliance_roster_message(
                cluster,
                db,
                alliance.id(),
                static_cast<dpp::snowflake>(alliance.thread_channel_id())
            );
        }

        std::string start_str = alliance_helpers::format_hhmm(new_scheduled_at);
        std::string sale_str  = alliance_helpers::format_hhmm(new_sale_at);

        std::ostringstream resp;
        resp << "✅ Date & heures mises à jour :\n"
             << "- Début : <t:" << new_scheduled_at << ":t> (" << start_str << ")\n"
             << "- Vente : <t:" << new_sale_at      << ":t> (" << sale_str  << ")";

        dpp::message msg(resp.str());
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return true;
    }

    if (cid.rfind("edit_alliance_custom_ship_role_", 0) == 0) {
        std::string ship_id_str = cid.substr(
            std::string("edit_alliance_custom_ship_role_").size()
        );

        std::uint64_t ship_id = 0;
        try {
            ship_id = std::stoull(ship_id_str);
        } catch (...) {
            dpp::message msg("❌ Impossible d'identifier le bateau à modifier.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        std::string role_input = trim(get_text_field(event, 0, 0));
        if (role_input.empty()) {
            dpp::message msg("❌ Merci de renseigner un nom de rôle.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        try {
            odb::transaction t(db->begin());

            std::unique_ptr<Ship> ship(db->load<Ship>(ship_id));
            if (!ship) {
                t.commit();
                dpp::message msg("❌ Ce bateau n'existe plus.");
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            ship->crew_role(role_input);
            std::uint64_t alliance_id = ship->alliance_id();
            db->update(*ship);

            t.commit();

            dpp::cluster* cluster = event.from()->creator;
            if (cluster) {
                try {
                    odb::transaction t2(db->begin());
                    std::unique_ptr<Alliance> a(
                        db->load<Alliance>(alliance_id)
                    );
                    dpp::snowflake thread_id = static_cast<dpp::snowflake>(a->thread_channel_id());
                    t2.commit();

                    alliance_helpers::create_or_update_alliance_roster_message(
                        cluster,
                        db,
                        alliance_id,
                        thread_id
                    );
                } catch (...) {
                }
            }

            dpp::message msg(
                "✅ Rôle du navire mis à jour : **" + role_input + "**."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
        }
        catch (const std::exception& ex) {
            std::cerr << "[EditAllianceUI::handle_modal] Erreur DB (custom ship role) : "
                      << ex.what() << "\n";
            dpp::message msg("❌ Erreur DB lors de la mise à jour du rôle.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
        }

        return true;
    }

    return false;
}
