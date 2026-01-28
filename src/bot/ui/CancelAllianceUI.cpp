#include "bot/ui/CancelAllianceUI.hpp"

#include <sstream>
#include <iostream>

#include <dpp/dpp.h>

#include <odb/pgsql/database.hxx>
#include <odb/transaction.hxx>
#include <odb/query.hxx>
#include <odb/exceptions.hxx>

#include "model/alliances.hxx"
#include "alliances-odb.hxx"

#include "bot/AllianceHelpers.hpp"

namespace {

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

template<typename Interaction>
static void perform_cancel_alliance(
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

    std::uint64_t alliance_id = 0;

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
                "La commande `/cancel` ne peut être utilisée que dans un thread d'alliance créé par le bot."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        Alliance alliance = *ait;
        alliance_id = alliance.id();

        std::uint64_t organizer_id  = alliance.organizer_id();
        std::uint64_t right_hand_id = 0;

        if (!alliance.right_hand().empty()) {
            right_hand_id = parse_mention_id(alliance.right_hand());
        }

        if (user_id != organizer_id && user_id != right_hand_id) {
            t.commit();
            dpp::message msg(
                "❌ Seul l'organisateur ou le bras droit peuvent lancer `/cancel` pour cette alliance."
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
                    "⚠️ Cette alliance a déjà été démarrée.\n"
                    "Utilise plutôt `/end` pour la terminer et supprimer les rôles/salons."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return;
            }

            case AllianceStatus::finished:
            case AllianceStatus::cancelled: {
                t.commit();
                dpp::message msg(
                    "❌ Cette alliance est déjà terminée ou annulée."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return;
            }
        }

        alliance.status(AllianceStatus::cancelled);
        db->update(alliance);

        t.commit();
    }
    catch (const std::exception& ex) {
        std::cerr << "[CancelAlliance] Erreur DB : " << ex.what() << "\n";
        dpp::message msg("❌ Erreur interne lors de l'annulation de l'alliance.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    {
        dpp::message msg;
        msg.set_flags(dpp::m_ephemeral);
        msg.set_content(
            "✅ Alliance annulée.\n"
            "Le thread a été marqué comme annulé et le message d'annonce mis à jour."
        );
        event.reply(msg);
    }

    if (cluster && alliance_id != 0) {
        dpp::snowflake thread_id(static_cast<dpp::snowflake>(channel_id));

        cluster->channel_get(
            thread_id,
            [cluster](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    std::cerr << "[CancelAlliance] Erreur récupération thread pour renommage : "
                              << cb.get_error().message << "\n";
                    return;
                }

                dpp::channel ch = cb.get<dpp::channel>();
                std::string old_name = ch.name;
                const std::string prefix = "❌ [Annulé] ";

                if (old_name.compare(0, prefix.size(), prefix) != 0) {
                    ch.set_name(prefix + old_name);

                    cluster->channel_edit(
                        ch,
                        [](const dpp::confirmation_callback_t& cb2) {
                            if (cb2.is_error()) {
                                std::cerr << "[CancelAlliance] Erreur renommage thread : "
                                          << cb2.get_error().message << "\n";
                            }
                        }
                    );
                }
            }
        );

        alliance_helpers::create_or_update_alliance_roster_message(
            cluster,
            db,
            alliance_id,
            thread_id
        );
    }
}


} // namespace

void CancelAllianceUI::open(const dpp::slashcommand_t& event,
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
                "La commande `/cancel` ne peut être utilisée que dans un thread d'alliance créé par le bot."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        Alliance alliance = *ait;

        std::uint64_t organizer_id  = alliance.organizer_id();
        std::uint64_t right_hand_id = 0;
        if (!alliance.right_hand().empty()) {
            right_hand_id = parse_mention_id(alliance.right_hand());
        }

        if (user_id != organizer_id && user_id != right_hand_id) {
            t.commit();
            dpp::message msg(
                "❌ Seul l'organisateur ou le bras droit peuvent lancer `/cancel` pour cette alliance."
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
                    "⚠️ Cette alliance a déjà été démarrée.\n"
                    "Utilise plutôt `/end` pour la terminer et supprimer les rôles/salons."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return;
            }

            case AllianceStatus::finished:
            case AllianceStatus::cancelled: {
                t.commit();
                dpp::message msg(
                    "❌ Cette alliance est déjà terminée ou annulée."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return;
            }
        }

        t.commit();

        std::ostringstream oss;
        oss << "⚠️ Es-tu sûr de vouloir **annuler** l'alliance **"
            << alliance.name() << "** ?\n\n"
            << "Aucun rôle ni salon vocal ne sera créé pour cette alliance.\n"
            << "Cette action est définitive : l'alliance sera marquée comme annulée.";

        dpp::message msg;
        msg.set_flags(dpp::m_ephemeral);
        msg.set_content(oss.str());

        dpp::component row;
        row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_danger)
                .set_id("cancel_alliance_confirm")
                .set_label("✅ Oui, annuler l'alliance")
        );
        row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_secondary)
                .set_id("cancel_alliance_cancel")
                .set_label("❌ Non, garder l'alliance")
        );

        msg.add_component(row);

        event.reply(msg);
    }
    catch (const std::exception& ex) {
        std::cerr << "[CancelAllianceUI::open] Erreur DB : " << ex.what() << "\n";
        dpp::message msg("❌ Erreur interne lors de la préparation de l'annulation.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
    }
}

bool CancelAllianceUI::handle_button(const dpp::button_click_t& event,
                                     const std::shared_ptr<odb::pgsql::database>& db)
{
    if (event.command.guild_id == 0)
        return false;

    const std::string& id = event.custom_id;

    if (id == "cancel_alliance_cancel") {
        dpp::message msg("❌ Action annulée, l'alliance n'a pas été modifiée.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return true;
    }

    if (id == "cancel_alliance_confirm") {
        perform_cancel_alliance(event, db);
        return true;
    }

    return false;
}

bool CancelAllianceUI::handle_select(const dpp::select_click_t& /*event*/,
                                     const std::shared_ptr<odb::pgsql::database>& /*db*/)
{
    return false;
}

bool CancelAllianceUI::handle_modal(const dpp::form_submit_t& /*event*/,
                                    const std::shared_ptr<odb::pgsql::database>& /*db*/) const
{
    return false;
}
