#include "bot/ui/EndAllianceUI.hpp"

#include <algorithm>
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <sstream>

#include <dpp/dpp.h>

#include <odb/pgsql/database.hxx>
#include <odb/transaction.hxx>
#include <odb/query.hxx>
#include <odb/exceptions.hxx>

#include "model/alliances.hxx"
#include "alliances-odb.hxx"

#include "model/alliance_discord_objects.hxx"
#include "alliance_discord_objects-odb.hxx"

namespace {

struct PendingDelete {
    DiscordObjectType type;
    std::uint64_t guild_id;
    std::uint64_t discord_id;
    std::uint64_t alliance_discord_obj_id;
    int attempts = 0;
};

std::mutex g_pending_delete_mutex;
std::vector<PendingDelete> g_pending_deletes;
bool g_retry_scheduled = false;

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

static void mark_object_deleted(
    const std::shared_ptr<odb::pgsql::database>& db,
    std::uint64_t obj_id
) {
    try {
        odb::transaction t(db->begin());
        std::unique_ptr<AllianceDiscordObject> obj(
            db->load<AllianceDiscordObject>(obj_id)
        );
        obj->mark_deleted_now();
        db->update(*obj);
        t.commit();
    } catch (const std::exception& ex) {
        std::cerr << "[EndAlliance] Erreur DB mark_object_deleted : "
                  << ex.what() << "\n";
    }
}

static void delete_discord_object_now(
    dpp::cluster* cluster,
    const std::shared_ptr<odb::pgsql::database>& db,
    PendingDelete pd
);

static void schedule_delete_retry(
    dpp::cluster* cluster,
    const std::shared_ptr<odb::pgsql::database>& db,
    const PendingDelete& pd
);

static void delete_discord_object_now(
    dpp::cluster* cluster,
    const std::shared_ptr<odb::pgsql::database>& db,
    PendingDelete pd
) {
    if (!cluster) return;

    using namespace std::string_literals;

    if (pd.type == DiscordObjectType::role) {
        dpp::snowflake guild_sf  = static_cast<dpp::snowflake>(pd.guild_id);
        dpp::snowflake role_sf   = static_cast<dpp::snowflake>(pd.discord_id);

        cluster->role_delete(
            guild_sf,
            role_sf,
            [db, cluster, pd](const dpp::confirmation_callback_t& cb) mutable {
                if (cb.is_error()) {
                    std::string msg = cb.get_error().message;

                    if (msg == "Unknown Role" || msg == "Unknown Role.") {
                        mark_object_deleted(db, pd.alliance_discord_obj_id);
                        return;
                    }

                    if (msg == "You are being rate limited." ||
                        msg == "You are being rate limited") {
                        pd.attempts++;
                        if (pd.attempts <= 3) {
                            std::cerr
                                << "[EndAlliance] Rate limited rôle "
                                << pd.discord_id
                                << ", retry #" << pd.attempts << "\n";
                            schedule_delete_retry(cluster, db, pd);
                        } else {
                            std::cerr
                                << "[EndAlliance] Abandon suppression rôle "
                                << pd.discord_id
                                << " après " << pd.attempts
                                << " tentatives.\n";
                        }
                        return;
                    }

                    std::cerr
                        << "[EndAlliance] Erreur suppression rôle "
                        << pd.discord_id << " : " << msg << "\n";
                    return;
                }

                mark_object_deleted(db, pd.alliance_discord_obj_id);
            }
        );
    }
    else {
        dpp::snowflake ch_sf = static_cast<dpp::snowflake>(pd.discord_id);

        cluster->channel_delete(
            ch_sf,
            [db, cluster, pd](const dpp::confirmation_callback_t& cb) mutable {
                if (cb.is_error()) {
                    std::string msg = cb.get_error().message;

                    if (msg == "Unknown Channel" || msg == "Unknown Channel.") {
                        mark_object_deleted(db, pd.alliance_discord_obj_id);
                        return;
                    }

                    if (msg == "You are being rate limited." ||
                        msg == "You are being rate limited") {
                        pd.attempts++;
                        if (pd.attempts <= 3) {
                            std::cerr
                                << "[EndAlliance] Rate limited channel "
                                << pd.discord_id
                                << ", retry #" << pd.attempts << "\n";
                            schedule_delete_retry(cluster, db, pd);
                        } else {
                            std::cerr
                                << "[EndAlliance] Abandon suppression channel "
                                << pd.discord_id
                                << " après " << pd.attempts
                                << " tentatives.\n";
                        }
                        return;
                    }

                    std::cerr
                        << "[EndAlliance] Erreur suppression channel "
                        << pd.discord_id << " : " << msg << "\n";
                    return;
                }

                mark_object_deleted(db, pd.alliance_discord_obj_id);
            }
        );
    }
}

static void schedule_delete_retry(
    dpp::cluster* cluster,
    const std::shared_ptr<odb::pgsql::database>& db,
    const PendingDelete& pd
) {
    if (!cluster) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_pending_delete_mutex);
        g_pending_deletes.push_back(pd);

        if (g_retry_scheduled) {
            return;
        }

        g_retry_scheduled = true;
    }

    std::thread([cluster, db]() {
        using namespace std::chrono_literals;

        std::this_thread::sleep_for(5s);

        while (true) {
            std::vector<PendingDelete> batch;

            {
                std::lock_guard<std::mutex> lock(g_pending_delete_mutex);
                if (g_pending_deletes.empty()) {
                    g_retry_scheduled = false;
                    break;
                }
                batch.swap(g_pending_deletes);
            }

            for (const auto& pending : batch) {
                delete_discord_object_now(cluster, db, pending);
            }

            std::this_thread::sleep_for(5s);
        }
    }).detach();
}

template<typename Interaction>
static void perform_end_alliance(
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

        using ObjQuery  = odb::query<AllianceDiscordObject>;
        using ObjResult = odb::result<AllianceDiscordObject>;

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
                "La commande `/end` ne peut être utilisée que dans un thread d'alliance créé par le bot."
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
                "❌ Seul l'organisateur ou le bras droit peuvent lancer `/end` pour cette alliance."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        bool already_finished = false;

        switch (alliance.status()) {
            case AllianceStatus::matching:
            case AllianceStatus::in_game:
                break;

            case AllianceStatus::planned: {
                t.commit();
                dpp::message msg(
                    "❌ Cette alliance n'a pas encore été démarrée (`/start`)."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return;
            }

            case AllianceStatus::finished:
            case AllianceStatus::cancelled: {
                already_finished = true;
                break;
            }
        }

        if (!already_finished &&
            alliance.status() != AllianceStatus::finished &&
            alliance.status() != AllianceStatus::cancelled)
        {
            alliance.status(AllianceStatus::finished);
            db->update(alliance);
        }

        ObjResult ores(
            db->query<AllianceDiscordObject>(
                ObjQuery::alliance_id == alliance_id &&
                ObjQuery::auto_delete == true &&
                ObjQuery::deleted_at == 0
            )
        );

        std::vector<AllianceDiscordObject> objects;
        for (const AllianceDiscordObject& o : ores) {
            objects.push_back(o);
        }

        t.commit();

        {
            dpp::message msg;
            msg.set_flags(dpp::m_ephemeral);

            if (already_finished) {
                msg.set_content(
                    "⚠️ Cette alliance était déjà terminée.\n"
                    "Je nettoie les rôles et salons restants créés pour cette alliance."
                );
            } else {
                msg.set_content(
                    "✅ Alliance terminée.\n"
                    "Les rôles et salons créés pour cette alliance vont être supprimés."
                );
            }

            event.reply(msg);

            {
                dpp::snowflake thread_id = static_cast<dpp::snowflake>(channel_id);

                cluster->channel_get(
                    thread_id,
                    [cluster](const dpp::confirmation_callback_t& cb) {
                        if (cb.is_error()) {
                            std::cerr << "[EndAlliance] Erreur récupération thread pour renommage : "
                                    << cb.get_error().message << "\n";
                            return;
                        }

                        dpp::channel ch = cb.get<dpp::channel>();
                        std::string old_name = ch.name;
                        const std::string prefix = "✅ [Terminé] ";

                        if (old_name.compare(0, prefix.size(), prefix) != 0) {
                            ch.set_name(prefix + old_name);

                            cluster->channel_edit(
                                ch,
                                [](const dpp::confirmation_callback_t& cb2) {
                                    if (cb2.is_error()) {
                                        std::cerr << "[EndAlliance] Erreur renommage thread : "
                                                << cb2.get_error().message << "\n";
                                    }
                                }
                            );
                        }
                    }
                );
            }

        }

        for (const auto& obj : objects) {
            DiscordObjectType type = obj.type();
            std::uint64_t discord_id = obj.discord_id();
            std::uint64_t obj_id = obj.id();

            bool is_channel =
                (type == DiscordObjectType::voice_channel) ||
                (type == DiscordObjectType::text_channel)  ||
                (type == DiscordObjectType::category);

            if (!is_channel)
                continue;

            PendingDelete pd;
            pd.type                    = type;
            pd.guild_id                = guild_id;
            pd.discord_id              = discord_id;
            pd.alliance_discord_obj_id = obj_id;
            pd.attempts                = 0;

            delete_discord_object_now(cluster, db, pd);
        }

        for (const auto& obj : objects) {
            if (obj.type() != DiscordObjectType::role)
                continue;

            std::uint64_t role_id = obj.discord_id();
            std::uint64_t obj_id  = obj.id();

            PendingDelete pd;
            pd.type                    = DiscordObjectType::role;
            pd.guild_id                = guild_id;
            pd.discord_id              = role_id;
            pd.alliance_discord_obj_id = obj_id;
            pd.attempts                = 0;

            delete_discord_object_now(cluster, db, pd);
        }

    }
    catch (const std::exception& ex) {
        std::cerr << "[EndAlliance] Erreur DB : " << ex.what() << "\n";
        dpp::message msg("❌ Erreur interne lors de la fin de l'alliance.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }
}

} // namespace

void EndAllianceUI::open(const dpp::slashcommand_t& event,
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
                "La commande `/end` ne peut être utilisée que dans un thread d'alliance créé par le bot."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        Alliance alliance = *ait;

        std::uint64_t organizer_id = alliance.organizer_id();
        std::uint64_t right_hand_id = 0;
        if (!alliance.right_hand().empty()) {
            right_hand_id = parse_mention_id(alliance.right_hand());
        }

        if (user_id != organizer_id && user_id != right_hand_id) {
            t.commit();
            dpp::message msg(
                "❌ Seul l'organisateur ou le bras droit peuvent lancer `/end` pour cette alliance."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        if (alliance.status() == AllianceStatus::planned) {
            t.commit();
            dpp::message msg(
                "❌ Cette alliance n'a pas encore été démarrée (`/start`)."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        t.commit();

        std::ostringstream oss;
        oss << "⚠️ Es-tu sûr de vouloir **mettre un terme** à l'alliance **"
            << alliance.name() << "** ?\n\n"
            << "Les rôles et salons créés pour cette alliance vont être supprimés.\n"
            << "Cette action est définitive : l'alliance ne pourra pas être redémarrée.";

        dpp::message msg;
        msg.set_flags(dpp::m_ephemeral);
        msg.set_content(oss.str());

        dpp::component row;
        row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_danger)
                .set_id("end_alliance_confirm")
                .set_label("✅ Oui, terminer l'alliance")
        );
        row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_secondary)
                .set_id("end_alliance_cancel")
                .set_label("❌ Annuler")
        );

        msg.add_component(row);

        event.reply(msg);
    }
    catch (const std::exception& ex) {
        std::cerr << "[EndAllianceUI::open] Erreur DB : " << ex.what() << "\n";
        dpp::message msg("❌ Erreur interne lors de la préparation de la fin de l'alliance.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
    }
}

bool EndAllianceUI::handle_button(const dpp::button_click_t& event,
                                  const std::shared_ptr<odb::pgsql::database>& db)
{
    if (event.command.guild_id == 0)
        return false;

    const std::string& id = event.custom_id;

    if (id == "end_alliance_cancel") {
        dpp::message msg("❌ Action annulée, l'alliance n'a pas été modifiée.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return true;
    }

    if (id == "end_alliance_confirm") {
        perform_end_alliance(event, db);
        return true;
    }

    return false;
}

bool EndAllianceUI::handle_select(const dpp::select_click_t& /*event*/,
                                  const std::shared_ptr<odb::pgsql::database>& /*db*/)
{
    return false;
}

bool EndAllianceUI::handle_modal(const dpp::form_submit_t& /*event*/,
                                 const std::shared_ptr<odb::pgsql::database>& /*db*/) const
{
    return false;
}
