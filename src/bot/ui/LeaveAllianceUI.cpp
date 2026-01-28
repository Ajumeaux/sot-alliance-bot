#include "bot/ui/LeaveAllianceUI.hpp"

#include <sstream>
#include <vector>
#include <algorithm>
#include <iostream>

#include <dpp/dpp.h>

#include <odb/pgsql/database.hxx>
#include <odb/transaction.hxx>
#include <odb/query.hxx>
#include <odb/exceptions.hxx>

#include "model/alliances.hxx"
#include "alliances-odb.hxx"

#include "model/ships.hxx"
#include "ships-odb.hxx"

#include "model/alliance_participants.hxx"
#include "alliance_participants-odb.hxx"

#include "model/users.hxx"
#include "users-odb.hxx"

#include "model/alliance_discord_objects.hxx"
#include "alliance_discord_objects-odb.hxx"

#include "bot/AllianceHelpers.hpp"

namespace {

template<typename Interaction>
static void perform_leave_alliance(
    const Interaction& event,
    const std::shared_ptr<odb::pgsql::database>& db
)
{
    if (event.command.guild_id == 0) {
        dpp::message msg("❌ Cette commande doit être utilisée dans un serveur, pas en DM.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    dpp::cluster* cluster = event.from()->creator;
    const std::uint64_t guild_id   = static_cast<std::uint64_t>(event.command.guild_id);
    const std::uint64_t channel_id = static_cast<std::uint64_t>(event.command.channel_id);
    const std::uint64_t user_id    = static_cast<std::uint64_t>(event.command.usr.id);

    try {
        using AllianceQuery  = odb::query<Alliance>;
        using AllianceResult = odb::result<Alliance>;
        using ShipQuery      = odb::query<Ship>;
        using PartQuery      = odb::query<AllianceParticipant>;
        using PartResult     = odb::result<AllianceParticipant>;

        odb::transaction t(db->begin());

        AllianceResult ares(
            db->query<Alliance>(
                AllianceQuery::guild_id == guild_id &&
                AllianceQuery::thread_channel_id == channel_id
            )
        );

        auto ait = ares.begin();
        if (ait == ares.end()) {
            t.commit();
            dpp::message msg(
                "❌ Ce thread n'est pas associé à une alliance connue.\n"
                "La commande `/leave` ne peut être utilisée que dans un thread d'alliance créé par le bot."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        Alliance alliance = *ait;
        const std::uint64_t alliance_id = alliance.id();
        AllianceStatus alliance_status  = alliance.status();
        std::string alliance_name       = alliance.name();

        if (alliance_status == AllianceStatus::finished ||
            alliance_status == AllianceStatus::cancelled)
        {
            t.commit();
            dpp::message msg(
                "❌ Cette alliance est terminée ou annulée, tu ne peux plus la quitter."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        std::unique_ptr<User> user;
        try {
            user.reset(db->load<User>(user_id));
        } catch (const odb::object_not_persistent&) {
            std::string uname = event.command.usr.username;
            user = std::make_unique<User>(user_id, uname);
            db->persist(*user);
        }

        if (user->is_banned()) {
            t.commit();
            dpp::message msg(
                "❌ Tu es banni des alliances.\n"
                "Raison : " + user->ban_reason()
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        PartResult pres(
            db->query<AllianceParticipant>(
                PartQuery::alliance_id == alliance_id &&
                PartQuery::user_id == user_id &&
                PartQuery::left_at == 0
            )
        );

        std::vector<AllianceParticipant> user_parts;
        for (const AllianceParticipant& p : pres) {
            user_parts.push_back(p);
        }

        if (user_parts.empty()) {
            t.commit();
            dpp::message msg(
                "❌ Tu n'es pas inscrit sur cette alliance."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        std::vector<std::uint64_t> ship_ids;
        ship_ids.reserve(user_parts.size());
        for (const auto& p : user_parts) {
            ship_ids.push_back(p.ship_id());
        }
        std::sort(ship_ids.begin(), ship_ids.end());
        ship_ids.erase(std::unique(ship_ids.begin(), ship_ids.end()), ship_ids.end());

        std::vector<std::string> ship_role_names;
        ship_role_names.reserve(ship_ids.size());

        for (std::uint64_t sid : ship_ids) {
            try {
                std::unique_ptr<Ship> ship(db->load<Ship>(sid));

                std::string hull = alliance_helpers::hull_label(ship->hull_type());
                std::string role = ship->crew_role().empty()
                                 ? "Libre"
                                 : ship->crew_role();

                std::ostringstream rn;
                rn << hull << " " << role; // ex: "Brigantin FDD"
                ship_role_names.push_back(rn.str());
            } catch (const odb::object_not_persistent&) {
                // Ship missing
            }
        }

        for (const auto& p : user_parts) {
            std::unique_ptr<AllianceParticipant> existing(
                db->load<AllianceParticipant>(p.id())
            );
            existing->left_now();
            db->update(*existing);
        }

        bool still_in_alliance = false;
        {
            PartResult pres2(
                db->query<AllianceParticipant>(
                    PartQuery::alliance_id == alliance_id &&
                    PartQuery::user_id == user_id &&
                    PartQuery::left_at == 0
                )
            );
            still_in_alliance = (pres2.begin() != pres2.end());
        }

        t.commit();

        if (cluster &&
            (alliance_status == AllianceStatus::matching ||
             alliance_status == AllianceStatus::in_game))
        {
            try {
                using ObjQuery  = odb::query<AllianceDiscordObject>;
                using ObjResult = odb::result<AllianceDiscordObject>;

                odb::transaction t2(db->begin());

                ObjResult ores(
                    db->query<AllianceDiscordObject>(
                        ObjQuery::alliance_id == alliance_id &&
                        ObjQuery::type == DiscordObjectType::role
                    )
                );

                std::uint64_t member_role_id = 0;
                std::vector<std::uint64_t> ship_role_ids;

                for (const AllianceDiscordObject& obj : ores) {
                    const std::string& rname = obj.name();

                    if (member_role_id == 0 && rname == alliance_name) {
                        member_role_id = obj.discord_id();
                    }

                    for (const auto& srn : ship_role_names) {
                        if (rname == srn) {
                            ship_role_ids.push_back(obj.discord_id());
                        }
                    }
                }

                t2.commit();

                auto remove_role = [cluster, guild_id, user_id](std::uint64_t role_id) {
                    if (role_id == 0)
                        return;

                    cluster->guild_member_remove_role(
                        static_cast<dpp::snowflake>(guild_id),
                        static_cast<dpp::snowflake>(user_id),
                        static_cast<dpp::snowflake>(role_id),
                        [](const dpp::confirmation_callback_t& cb) {
                            if (cb.is_error()) {
                                std::cerr << "[LeaveAllianceUI] Erreur retrait rôle : "
                                          << cb.get_error().message << "\n";
                            }
                        }
                    );
                };

                for (std::uint64_t rid : ship_role_ids) {
                    remove_role(rid);
                }

                if (!still_in_alliance) {
                    remove_role(member_role_id);
                }
            }
            catch (const std::exception& ex) {
                std::cerr << "[LeaveAllianceUI] Erreur DB retrait rôles : "
                          << ex.what() << "\n";
            }
        }

        if (cluster) {
            alliance_helpers::create_or_update_alliance_roster_message(
                cluster,
                db,
                alliance_id,
                event.command.channel_id
            );
        }

        std::ostringstream oss;
        oss << "✅ Tu as quitté l'alliance **" << alliance_name << "**.";

        dpp::message msg;
        msg.set_flags(dpp::m_ephemeral);
        msg.set_content(oss.str());
        event.reply(msg);
    }
    catch (const std::exception& ex) {
        std::cerr << "[LeaveAllianceUI] Erreur DB : " << ex.what() << "\n";
        dpp::message msg("❌ Erreur interne lors de la sortie de l'alliance.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }
}

} // namespace

void LeaveAllianceUI::open(const dpp::slashcommand_t& event,
                           const std::shared_ptr<odb::pgsql::database>& db)
{
    if (event.command.guild_id == 0) {
        dpp::message msg("❌ Cette commande doit être utilisée dans un serveur, pas en DM.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    const std::uint64_t guild_id   = static_cast<std::uint64_t>(event.command.guild_id);
    const std::uint64_t channel_id = static_cast<std::uint64_t>(event.command.channel_id);
    const std::uint64_t user_id    = static_cast<std::uint64_t>(event.command.usr.id);
    (void)user_id;

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

        auto ait = ares.begin();
        if (ait == ares.end()) {
            t.commit();
            dpp::message msg(
                "❌ Ce thread n'est pas associé à une alliance connue.\n"
                "La commande `/leave` ne peut être utilisée que dans un thread d'alliance créé par le bot."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        Alliance alliance = *ait;

        t.commit();

        std::ostringstream oss;
        oss << "⚠️ Es-tu sûr de vouloir **quitter** l'alliance **"
            << alliance.name() << "** ?\n\n"
            << "Tu seras retiré de ton bateau et tu ne feras plus partie de cette alliance.";

        dpp::message msg;
        msg.set_flags(dpp::m_ephemeral);
        msg.set_content(oss.str());

        dpp::component row;
        row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_danger)
                .set_id("leave_alliance_confirm")
                .set_label("✅ Oui, quitter l'alliance")
        );
        row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_secondary)
                .set_id("leave_alliance_cancel")
                .set_label("❌ Annuler")
        );

        msg.add_component(row);
        event.reply(msg);
    }
    catch (const std::exception& ex) {
        std::cerr << "[LeaveAllianceUI::open] Erreur DB : " << ex.what() << "\n";
        dpp::message msg("❌ Erreur interne lors de la préparation de la sortie d'alliance.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
    }
}

bool LeaveAllianceUI::handle_button(const dpp::button_click_t& event,
                                    const std::shared_ptr<odb::pgsql::database>& db)
{
    if (event.command.guild_id == 0)
        return false;

    const std::string& id = event.custom_id;

    if (id == "leave_alliance_cancel") {
        dpp::message msg("❌ Action annulée, tu restes inscrit sur cette alliance.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return true;
    }

    if (id == "leave_alliance_confirm") {
        perform_leave_alliance(event, db);
        return true;
    }

    return false;
}

bool LeaveAllianceUI::handle_select(const dpp::select_click_t& /*event*/,
                                    const std::shared_ptr<odb::pgsql::database>& /*db*/)
{
    return false;
}

bool LeaveAllianceUI::handle_modal(const dpp::form_submit_t& /*event*/,
                                   const std::shared_ptr<odb::pgsql::database>& /*db*/) const
{
    return false;
}
