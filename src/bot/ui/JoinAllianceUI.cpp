#include "bot/ui/JoinAllianceUI.hpp"

#include <sstream>
#include <unordered_map>
#include <vector>
#include <iostream>

#include <dpp/dpp.h>

#include <odb/pgsql/database.hxx>
#include <odb/transaction.hxx>
#include <odb/exceptions.hxx>
#include <odb/query.hxx>

#include "model/alliances.hxx"
#include "alliances-odb.hxx"

#include "model/ships.hxx"
#include "ships-odb.hxx"

#include "model/alliance_participants.hxx"
#include "alliance_participants-odb.hxx"

#include "model/bot_settings.hxx"
#include "bot_settings-odb.hxx"

#include "model/users.hxx"
#include "users-odb.hxx"

#include "model/alliance_discord_objects.hxx"
#include "alliance_discord_objects-odb.hxx"

#include "bot/AllianceHelpers.hpp"

void JoinAllianceUI::open(const dpp::slashcommand_t& event,
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

    try {
        typedef odb::query<Alliance> AllianceQuery;
        typedef odb::result<Alliance> AllianceResult;

        typedef odb::query<Ship> ShipQuery;
        typedef odb::result<Ship> ShipResult;

        typedef odb::query<AllianceParticipant> PartQuery;
        typedef odb::result<AllianceParticipant> PartResult;

        typedef odb::query<BotSettings> SettingsQuery;

        odb::transaction t(db->begin());

        AllianceResult ares(
            db->query<Alliance>(
                AllianceQuery::guild_id == guild_id &&
                AllianceQuery::thread_channel_id == channel_id
            )
        );

        auto ait = ares.begin();
        if (ait == ares.end()) {
            dpp::message msg(
                "❌ Ce thread n'est pas associé à une alliance connue.\n"
                "La commande `/join` ne peut être utilisée que dans un thread d'alliance créé par le bot."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        Alliance alliance = *ait;
        const std::uint64_t alliance_id = alliance.id();

        if (alliance.status() == AllianceStatus::finished ||
            alliance.status() == AllianceStatus::cancelled)
        {
            dpp::message msg("❌ Cette alliance est terminée ou annulée, tu ne peux plus la rejoindre.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        bool allow_public_join = true;
        try {
            std::unique_ptr<BotSettings> settings(db->load<BotSettings>(guild_id));
            allow_public_join = settings->allow_public_join();
        } catch (const odb::object_not_persistent&) {
        }

        if (!allow_public_join) {
            dpp::message msg(
                "❌ Les inscriptions publiques sont désactivées pour ce serveur."
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
            dpp::message msg(
                "❌ Tu es banni des alliances.\n"
                "Raison : " + user->ban_reason()
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        ShipQuery q(ShipQuery::alliance_id == alliance_id);
        q += " ORDER BY " + ShipQuery::slot;

        ShipResult sres(db->query<Ship>(q));

        std::vector<Ship> ships;
        for (const Ship& s : sres) {
            ships.push_back(s);
        }

        if (ships.empty()) {
            dpp::message msg(
                "❌ Aucun bateau n'est configuré pour cette alliance.\n"
                "Demande à l'organisateur de recréer l'alliance."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        PartResult pres(
            db->query<AllianceParticipant>(
                PartQuery::alliance_id == alliance_id &&
                PartQuery::left_at == 0
            )
        );

        std::unordered_map<std::uint64_t, std::vector<AllianceParticipant>> by_ship;
        std::uint64_t current_ship_id = 0;

        for (const AllianceParticipant& p : pres) {
            by_ship[p.ship_id()].push_back(p);
            if (p.user_id() == user_id && p.left_at() == 0) {
                current_ship_id = p.ship_id();
            }
        }

        t.commit();

        dpp::message m;
        m.set_flags(dpp::m_ephemeral);

        std::ostringstream intro;
        intro << "Choisis le **bateau** que tu veux rejoindre pour l'alliance **"
              << alliance.name() << "**.\n";

        if (current_ship_id != 0) {
            const Ship* cur_ship = nullptr;
            for (const auto& s : ships) {
                if (s.id() == current_ship_id) {
                    cur_ship = &s;
                    break;
                }
            }
            if (cur_ship) {
                intro << "Tu es actuellement inscrit sur : **"
                      << alliance_helpers::hull_label(cur_ship->hull_type()) << " - "
                      << cur_ship->crew_role() << "**.\n"
                      << "Tu peux utiliser ce sélecteur pour changer de bateau.";
            }
        } else {
            intro << "Tu n'es pas encore inscrit sur un bateau pour cette alliance.";
        }

        m.set_content(intro.str());

        dpp::component select;
        select.set_type(dpp::cot_selectmenu)
              .set_id("join_alliance_ship_select")
              .set_placeholder("Choisis un bateau")
              .set_min_values(1)
              .set_max_values(1);

        for (const Ship& ship : ships) {
            if (ship.id() == current_ship_id)
                continue;

            int cap = alliance_helpers::hull_capacity(ship.hull_type());
            int count = 0;
            auto it = by_ship.find(ship.id());
            if (it != by_ship.end()) {
                count = static_cast<int>(it->second.size());
            }

            std::ostringstream label;
            label << alliance_helpers::hull_label(ship.hull_type())
                  << " - " << ship.crew_role()
                  << " (" << count << "/" << cap << ")";

            select.add_select_option(
                dpp::select_option(label.str(), std::to_string(ship.id()))
            );
        }

        if (select.options.empty()) {
            m.set_content(
                "Tu es déjà inscrit sur le seul bateau disponible pour cette alliance. "
                "Il n'y a pas d'autre bateau à rejoindre pour le moment."
            );
            event.reply(m);
            return;
        }

        m.add_component(dpp::component().add_component(select));
        event.reply(m);
    }
    catch (const std::exception& ex) {
        std::cerr << "[JoinAllianceUI] Erreur DB dans open : " << ex.what() << "\n";
        dpp::message msg("❌ Erreur interne en ouvrant le sélecteur de bateaux.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
    }
}

bool JoinAllianceUI::handle_select(const dpp::select_click_t& event,
                                   const std::shared_ptr<odb::pgsql::database>& db)
{
    const std::string& id = event.custom_id;

    if (event.command.guild_id == 0)
        return false;

    if (id != "join_alliance_ship_select")
        return false;

    if (event.values.empty()) {
        dpp::message msg("❌ Tu n'as rien sélectionné.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return true;
    }

    std::uint64_t ship_id = 0;
    try {
        ship_id = std::stoull(event.values[0]);
    } catch (...) {
        dpp::message msg("❌ Valeur de sélection invalide.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return true;
    }

    const std::uint64_t guild_id   = static_cast<std::uint64_t>(event.command.guild_id);
    const std::uint64_t channel_id = static_cast<std::uint64_t>(event.command.channel_id);
    const std::uint64_t user_id    = static_cast<std::uint64_t>(event.command.usr.id);

    try {
        typedef odb::query<Alliance> AllianceQuery;
        typedef odb::result<Alliance> AllianceResult;

        typedef odb::query<AllianceParticipant> PartQuery;
        typedef odb::result<AllianceParticipant> PartResult;

        odb::transaction t(db->begin());

        AllianceResult ares(
            db->query<Alliance>(
                AllianceQuery::guild_id == guild_id &&
                AllianceQuery::thread_channel_id == channel_id
            )
        );
        auto ait = ares.begin();
        if (ait == ares.end()) {
            dpp::message msg(
                "❌ Ce thread n'est pas associé à une alliance connue."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }
        Alliance alliance = *ait;
        const std::uint64_t alliance_id = alliance.id();

        AllianceStatus alliance_status = alliance.status();
        std::string alliance_name = alliance.name();

        std::unique_ptr<Ship> ship;
        try {
            ship.reset(db->load<Ship>(ship_id));
        } catch (const odb::object_not_persistent&) {
            dpp::message msg("❌ Ce bateau n'existe pas ou plus.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        std::string ship_role_name;
        {
            std::string hull = alliance_helpers::hull_label(ship->hull_type());
            std::string role = ship->crew_role().empty() ? "Libre" : ship->crew_role();

            std::ostringstream rn;
            rn << hull << " " << role;
            ship_role_name = rn.str();
        }


        if (ship->alliance_id() != alliance_id) {
            dpp::message msg("❌ Ce bateau n'appartient pas à cette alliance.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
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
            dpp::message msg(
                "❌ Tu es banni des alliances.\n"
                "Raison : " + user->ban_reason()
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        PartResult pres(
            db->query<AllianceParticipant>(
                PartQuery::alliance_id == alliance_id &&
                PartQuery::left_at == 0
            )
        );

        std::vector<AllianceParticipant> participants;
        participants.reserve(32);

        std::uint64_t current_participant_id = 0;
        std::uint64_t old_ship_id = 0;

        for (const AllianceParticipant& p : pres) {
            participants.push_back(p);
            if (p.user_id() == user_id && p.left_at() == 0) {
                current_participant_id = p.id();
                old_ship_id = p.ship_id();
            }
        }

        if (old_ship_id == ship_id) {
            dpp::message msg("Tu es déjà inscrit sur ce bateau.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        if (current_participant_id != 0) {
            std::unique_ptr<AllianceParticipant> existing(
                db->load<AllianceParticipant>(current_participant_id)
            );
            existing->left_now();
            db->update(*existing);
        }

        int crew_count = 0;
        for (const auto& p : participants) {
            if (p.ship_id() == ship_id && p.left_at() == 0) {
                crew_count++;
            }
        }

        int cap = alliance_helpers::hull_capacity(ship->hull_type());

        AllianceParticipant new_part(alliance_id, user_id, ship_id);
        db->persist(new_part);
        crew_count++;

        user->last_alliance_now();
        db->update(*user);

        t.commit();



        if (auto* cluster = event.from()->creator) {
            if (alliance_status == AllianceStatus::matching ||
                alliance_status == AllianceStatus::in_game)
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
                    std::uint64_t ship_role_id   = 0;

                    for (const AllianceDiscordObject& obj : ores) {
                        if (member_role_id == 0 && obj.name() == alliance_name) {
                            member_role_id = obj.discord_id();
                        }

                        if (ship_role_id == 0 && obj.name() == ship_role_name) {
                            ship_role_id = obj.discord_id();
                        }
                    }

                    t2.commit();

                    auto add_role = [cluster, guild_id, user_id](std::uint64_t role_id) {
                        if (role_id == 0)
                            return;

                        cluster->guild_member_add_role(
                            static_cast<dpp::snowflake>(guild_id),
                            static_cast<dpp::snowflake>(user_id),
                            static_cast<dpp::snowflake>(role_id),
                            [](const dpp::confirmation_callback_t& cb) {
                                if (cb.is_error()) {
                                    std::cerr << "[JoinAllianceUI] Erreur ajout rôle : "
                                            << cb.get_error().message << "\n";
                                }
                            }
                        );
                    };

                    add_role(member_role_id);
                    add_role(ship_role_id);
                }
                catch (const std::exception& ex) {
                    std::cerr << "[JoinAllianceUI] Erreur DB assignation rôles post-join : "
                            << ex.what() << "\n";
                }
            }

            alliance_helpers::create_or_update_alliance_roster_message(
                cluster,
                db,
                alliance_id,
                event.command.channel_id
            );
        }

        bool is_replacement = (crew_count > cap);

        std::ostringstream oss;
        if (is_replacement) {
            oss << "✅ Tu as été ajouté(e) comme **remplaçant(e)** sur **"
                << alliance_helpers::hull_label(ship->hull_type()) << " - " << ship->crew_role()
                << "**.";
        } else {
            oss << "✅ Tu as rejoint l'équipage de **"
                << alliance_helpers::hull_label(ship->hull_type()) << " - " << ship->crew_role()
                << "**.";
        }

        if (old_ship_id != 0 && old_ship_id != ship_id) {
            oss << "\nTu as été retiré(e) de ton ancien bateau.";
        }

        dpp::message msg;
        msg.set_content(oss.str());
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);

        return true;
    }
    catch (const std::exception& ex) {
        std::cerr << "[JoinAllianceUI] Erreur DB dans handle_select : "
                  << ex.what() << "\n";
        dpp::message msg("❌ Erreur interne lors de l'inscription à l'alliance.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return true;
    }
}
