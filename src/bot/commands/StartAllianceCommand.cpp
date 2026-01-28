#include "bot/commands/StartAllianceCommand.hpp"

#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <random>

#include <dpp/dpp.h>

#include <odb/transaction.hxx>
#include <odb/query.hxx>
#include <odb/exceptions.hxx>

#include "model/alliances.hxx"
#include "alliances-odb.hxx"

#include "model/ships.hxx"
#include "ships-odb.hxx"

#include "model/alliance_participants.hxx"
#include "alliance_participants-odb.hxx"

#include "model/alliance_discord_objects.hxx"
#include "alliance_discord_objects-odb.hxx"

#include "bot/AllianceHelpers.hpp"

namespace {


struct CrewEntry {
    std::uint64_t user_id;
    std::uint64_t ship_id;
};

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

static void persist_discord_object(
    const std::shared_ptr<odb::pgsql::database>& db,
    std::uint64_t alliance_id,
    DiscordObjectType type,
    std::uint64_t discord_id,
    const std::string& name
){
    try {
        odb::transaction t(db->begin());
        AllianceDiscordObject obj(
            alliance_id,
            type,
            discord_id,
            name,
            true
        );
        db->persist(obj);
        t.commit();
    } catch (const std::exception& ex) {
        std::cerr << "[StartAlliance] Erreur DB persist_discord_object : "
                  << ex.what() << "\n";
    }
}

template<typename F>
static void create_role_and_record(
    dpp::cluster* cluster,
    const std::shared_ptr<odb::pgsql::database>& db,
    std::uint64_t guild_id,
    std::uint64_t alliance_id,
    const std::string& role_name,
    F&& on_created
)
{
    if (!cluster) return;

    dpp::role r;
    r.set_name(role_name);
    r.guild_id = static_cast<dpp::snowflake>(guild_id);

    if (role_name == "Organisateur") {
        r.set_colour(0xF1C40F);

        r.flags |= dpp::r_hoist;
        r.flags |= dpp::r_mentionable;

    } else if (role_name == "Bras droit") {
        r.set_colour(0x1ABC9C);

        r.flags |= dpp::r_hoist;
        r.flags |= dpp::r_mentionable;
    }

    cluster->role_create(
        r,
        [db, alliance_id, role_name, on_created](const dpp::confirmation_callback_t& cb) mutable {
            if (cb.is_error()) {
                std::cerr << "[StartAlliance] Erreur création rôle '" << role_name
                          << "' : " << cb.get_error().message << "\n";
                return;
            }

            dpp::role created = cb.get<dpp::role>();
            std::uint64_t role_id = static_cast<std::uint64_t>(created.id);

            persist_discord_object(
                db,
                alliance_id,
                DiscordObjectType::role,
                role_id,
                role_name
            );

            on_created(role_id);
        }
    );
}


template<typename F>
static void create_category_and_record(
    dpp::cluster* cluster,
    const std::shared_ptr<odb::pgsql::database>& db,
    std::uint64_t guild_id,
    std::uint64_t alliance_id,
    const std::string& name,
    F&& on_created
)
{
    if (!cluster) return;

    dpp::channel cat;
    cat.set_name(name);
    cat.set_type(dpp::CHANNEL_CATEGORY);
    cat.set_guild_id(static_cast<dpp::snowflake>(guild_id));

    cluster->channel_create(
        cat,
        [db, alliance_id, name, on_created](const dpp::confirmation_callback_t& cb) mutable {
            if (cb.is_error()) {
                std::cerr << "[StartAlliance] Erreur création catégorie '" << name
                          << "' : " << cb.get_error().message << "\n";
                return;
            }

            dpp::channel created = cb.get<dpp::channel>();
            std::uint64_t cat_id = static_cast<std::uint64_t>(created.id);

            persist_discord_object(
                db,
                alliance_id,
                DiscordObjectType::category,
                cat_id,
                name
            );

            on_created(cat_id);
        }
    );
}

static void create_generic_voice_channel(
    dpp::cluster* cluster,
    const std::shared_ptr<odb::pgsql::database>& db,
    std::uint64_t guild_id,
    std::uint64_t alliance_id,
    std::uint64_t category_id,
    std::uint64_t member_role_id,
    const std::string& vc_name,
    std::uint16_t position
)
{
    if (!cluster) return;

    dpp::channel vc;
    vc.set_name(vc_name);
    vc.set_type(dpp::CHANNEL_VOICE);
    vc.set_parent_id(static_cast<dpp::snowflake>(category_id));
    vc.set_guild_id(static_cast<dpp::snowflake>(guild_id));
    vc.set_position(position);

    dpp::permission_overwrite po_everyone;
    po_everyone.id   = static_cast<dpp::snowflake>(guild_id); // @everyone
    po_everyone.type = dpp::ot_role;
    po_everyone.deny = dpp::p_connect | dpp::p_view_channel;

    dpp::permission_overwrite po_member;
    po_member.id   = static_cast<dpp::snowflake>(member_role_id);
    po_member.type = dpp::ot_role;
    po_member.allow = dpp::p_connect | dpp::p_view_channel;

    vc.permission_overwrites.clear();
    vc.permission_overwrites.push_back(po_everyone);
    vc.permission_overwrites.push_back(po_member);

    cluster->channel_create(
        vc,
        [db, alliance_id, vc_name](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                std::cerr << "[StartAlliance] Erreur création salon vocal '"
                          << vc_name << "' : " << cb.get_error().message << "\n";
                return;
            }

            dpp::channel created = cb.get<dpp::channel>();
            std::uint64_t ch_id = static_cast<std::uint64_t>(created.id);

            persist_discord_object(
                db,
                alliance_id,
                DiscordObjectType::voice_channel,
                ch_id,
                vc_name
            );
        }
    );
}

static void create_voice_for_ship(
    dpp::cluster* cluster,
    const std::shared_ptr<odb::pgsql::database>& db,
    std::uint64_t guild_id,
    std::uint64_t alliance_id,
    std::uint64_t category_id,
    std::uint64_t member_role_id,
    const Ship& ship,
    std::uint16_t position 
)
{
    if (!cluster) return;

    std::string hull = alliance_helpers::hull_label(ship.hull_type());
    std::string role = ship.crew_role().empty()
                     ? "Libre"
                     : ship.crew_role();

    std::ostringstream name_oss;
    name_oss << hull << " - " << role;
    std::string vc_name = name_oss.str();

    dpp::channel vc;
    vc.set_name(vc_name);
    vc.set_type(dpp::CHANNEL_VOICE);
    vc.set_parent_id(static_cast<dpp::snowflake>(category_id));
    vc.set_guild_id(static_cast<dpp::snowflake>(guild_id));
    vc.set_position(position);

    dpp::permission_overwrite po_everyone;
    po_everyone.id   = static_cast<dpp::snowflake>(guild_id); // @everyone
    po_everyone.type = dpp::ot_role;
    po_everyone.deny = dpp::p_connect | dpp::p_view_channel;

    dpp::permission_overwrite po_member;
    po_member.id   = static_cast<dpp::snowflake>(member_role_id);
    po_member.type = dpp::ot_role;
    po_member.allow = dpp::p_connect | dpp::p_view_channel;

    vc.permission_overwrites.clear();
    vc.permission_overwrites.push_back(po_everyone);
    vc.permission_overwrites.push_back(po_member);

    cluster->channel_create(
        vc,
        [db, alliance_id, vc_name](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                std::cerr << "[StartAlliance] Erreur création salon vocal '"
                          << vc_name << "' : " << cb.get_error().message << "\n";
                return;
            }

            dpp::channel created = cb.get<dpp::channel>();
            std::uint64_t ch_id = static_cast<std::uint64_t>(created.id);

            persist_discord_object(
                db,
                alliance_id,
                DiscordObjectType::voice_channel,
                ch_id,
                vc_name
            );
        }
    );
}


static void add_role_to_users(
    dpp::cluster* cluster,
    std::uint64_t guild_id,
    std::uint64_t role_id,
    const std::vector<std::uint64_t>& user_ids
)
{
    if (!cluster) return;

    for (std::uint64_t uid : user_ids) {
        cluster->guild_member_add_role(
            guild_id,
            static_cast<dpp::snowflake>(uid),
            static_cast<dpp::snowflake>(role_id),
            [](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    // nothing
                }
            }
        );
    }
}

} // namespace


void StartAllianceCommand::handle(
    const dpp::slashcommand_t& event,
    const std::shared_ptr<odb::pgsql::database>& db
) const
{
    if (event.command.guild_id == 0) {
        dpp::message msg("❌ Cette commande doit être utilisée dans un serveur, pas en DM.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    dpp::cluster* cluster = event.from()->creator;
    if (!cluster) {
        dpp::message msg("❌ Erreur interne (cluster Discord indisponible).");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    const std::uint64_t guild_id   = static_cast<std::uint64_t>(event.command.guild_id);
    const std::uint64_t channel_id = static_cast<std::uint64_t>(event.command.channel_id);
    const std::uint64_t user_id    = static_cast<std::uint64_t>(event.command.usr.id);

    try {
        using AllianceQuery  = odb::query<Alliance>;
        using AllianceResult = odb::result<Alliance>;

        using ShipQuery  = odb::query<Ship>;
        using ShipResult = odb::result<Ship>;

        using PartQuery  = odb::query<AllianceParticipant>;
        using PartResult = odb::result<AllianceParticipant>;

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
                "La commande `/start` ne peut être utilisée que dans un thread d'alliance créé par le bot."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        Alliance alliance = *ait;
        std::uint64_t alliance_id  = alliance.id();
        std::uint64_t organizer_id = alliance.organizer_id();
        std::uint64_t right_hand_id = 0;

        if (!alliance.right_hand().empty()) {
            right_hand_id = parse_mention_id(alliance.right_hand());
        }

        if (user_id != organizer_id && user_id != right_hand_id) {
            t.commit();
            dpp::message msg(
                "❌ Seul l'organisateur ou le bras droit peuvent lancer `/start` pour cette alliance."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        switch (alliance.status()) {
            case AllianceStatus::planned:
                break;

            case AllianceStatus::matching:
            case AllianceStatus::in_game: {
                t.commit();
                dpp::message msg(
                    "⚠️ Cette alliance est déjà démarrée. "
                    "(Les rôles et salons ont déjà été créés.)"
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return;
            }

            case AllianceStatus::finished:
            case AllianceStatus::cancelled: {
                t.commit();
                dpp::message msg(
                    "❌ Cette alliance est terminée ou annulée, tu ne peux plus la démarrer."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return;
            }
        }

        ShipQuery sq(ShipQuery::alliance_id == alliance_id);
        sq += " ORDER BY " + ShipQuery::slot;

        ShipResult sres(db->query<Ship>(sq));

        std::vector<Ship> ships;
        for (const Ship& s : sres) {
            ships.push_back(s);
        }

        if (ships.empty()) {
            t.commit();
            dpp::message msg(
                "❌ Aucun bateau n'est configuré pour cette alliance.\n"
                "Impossible de créer les salons vocaux."
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

        std::vector<CrewEntry> crew;
        std::vector<std::uint64_t> all_member_ids;
        for (const AllianceParticipant& p : pres) {
            CrewEntry e;
            e.user_id = p.user_id();
            e.ship_id = p.ship_id();
            crew.push_back(e);

            all_member_ids.push_back(p.user_id());
        }

        if (std::find(all_member_ids.begin(), all_member_ids.end(), organizer_id) == all_member_ids.end()) {
            all_member_ids.push_back(organizer_id);
        }
        if (right_hand_id != 0 &&
            std::find(all_member_ids.begin(), all_member_ids.end(), right_hand_id) == all_member_ids.end())
        {
            all_member_ids.push_back(right_hand_id);
        }

        std::sort(all_member_ids.begin(), all_member_ids.end());
        all_member_ids.erase(
            std::unique(all_member_ids.begin(), all_member_ids.end()),
            all_member_ids.end()
        );

        alliance.status(AllianceStatus::matching);
        db->update(alliance);

        t.commit();

        {
            dpp::message msg;
            msg.set_flags(dpp::m_ephemeral);
            msg.set_content(
                "🛠️ Initialisation de l'alliance en cours...\n"
                "Création des rôles et des salons vocaux."
            );
            event.reply(msg);
        }

        std::string base_name   = alliance.name();
        std::string member_role = base_name;
        std::string orga_role   = "Organisateur";
        std::string bras_role   = "Bras droit";

        create_role_and_record(
            cluster,
            db,
            guild_id,
            alliance_id,
            member_role,
            [cluster,
             db,
             guild_id,
             alliance_id,
             alliance,
             ships,
             crew,
             all_member_ids,
             organizer_id,
             right_hand_id,
             orga_role,
             bras_role](std::uint64_t member_role_id)
            {
                add_role_to_users(cluster, guild_id, member_role_id, all_member_ids);

                if (!orga_role.empty()) {
                    create_role_and_record(
                        cluster,
                        db,
                        guild_id,
                        alliance_id,
                        orga_role,
                        [cluster, guild_id, organizer_id](std::uint64_t orga_role_id) {
                            std::vector<std::uint64_t> v { organizer_id };
                            add_role_to_users(cluster, guild_id, orga_role_id, v);
                        }
                    );
                }

                if (right_hand_id != 0 && !bras_role.empty()) {
                    create_role_and_record(
                        cluster,
                        db,
                        guild_id,
                        alliance_id,
                        bras_role,
                        [cluster, guild_id, right_hand_id](std::uint64_t bras_role_id) {
                            std::vector<std::uint64_t> v { right_hand_id };
                            add_role_to_users(cluster, guild_id, bras_role_id, v);
                        }
                    );
                }

                for (const Ship& ship : ships) {
                    std::string hull = alliance_helpers::hull_label(ship.hull_type());
                    std::string role = ship.crew_role().empty()
                                     ? "Libre"
                                     : ship.crew_role();

                    std::ostringstream rn;
                    rn << hull << " " << role; // ex: "Brigantin FDD"
                    std::string ship_role_name = rn.str();
                    std::uint64_t ship_id = ship.id();

                    std::vector<std::uint64_t> ship_users;
                    for (const auto& c : crew) {
                        if (c.ship_id == ship_id) {
                            ship_users.push_back(c.user_id);
                        }
                    }

                    create_role_and_record(
                        cluster,
                        db,
                        guild_id,
                        alliance_id,
                        ship_role_name,
                        [cluster, guild_id, ship_users](std::uint64_t ship_role_id) {
                            if (!ship_users.empty()) {
                                add_role_to_users(cluster, guild_id, ship_role_id, ship_users);
                            }
                        }
                    );
                }

                create_category_and_record(
                    cluster,
                    db,
                    guild_id,
                    alliance_id,
                    alliance.name(),
                    [cluster,
                    db,
                    guild_id,
                    alliance_id,
                    member_role_id,
                    ships](std::uint64_t category_id)
                    {
                        std::uint16_t position = 0;

                        {
                            static const std::vector<std::string> avant_postes = {
                                "Avant-poste Golden Sands",
                                "Avant-poste Sanctuary",
                                "Avant-poste Ancient Spire",
                                "Avant-poste Plunder",
                                "Avant-poste Dagger Tooth",
                                "Avant-poste Galleon's Grave",
                                "Avant-poste Morrow's Peak"
                            };

                            if (!avant_postes.empty()) {
                                std::random_device rd;
                                std::mt19937 gen(rd());
                                std::uniform_int_distribution<std::size_t> dist(0, avant_postes.size() - 1);

                                std::string hub_name = avant_postes[dist(gen)];

                                create_generic_voice_channel(
                                    cluster,
                                    db,
                                    guild_id,
                                    alliance_id,
                                    category_id,
                                    member_role_id,
                                    hub_name,
                                    position++
                                );
                            }
                        }

                        for (const Ship& ship : ships) {
                            create_voice_for_ship(
                                cluster,
                                db,
                                guild_id,
                                alliance_id,
                                category_id,
                                member_role_id,
                                ship,
                                position++
                            );
                        }
                    }
                );

            }
        );
    }
    catch (const std::exception& ex) {
        std::cerr << "[StartAlliance] Erreur DB : " << ex.what() << "\n";
        dpp::message msg("❌ Erreur interne lors du démarrage de l'alliance.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }
}
