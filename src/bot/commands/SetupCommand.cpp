#include "bot/commands/SetupCommand.hpp"

#include <dpp/dpp.h>

void SetupCommand::handle(const dpp::slashcommand_t& event,
                          const std::shared_ptr<odb::pgsql::database>& /*db*/) const
{
    if (event.command.guild_id == 0) {
        dpp::message msg("❌ Cette commande doit être utilisée dans un serveur, pas en DM.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    dpp::guild* g = dpp::find_guild(event.command.guild_id);
    dpp::channel* ch = dpp::find_channel(event.command.channel_id);

    bool is_admin = false;

    if (g && g->owner_id == event.command.usr.id) {
        is_admin = true;
    }
    else if (ch) {
        uint64_t perms = ch->get_user_permissions(&event.command.usr);
        if (perms & dpp::p_administrator) {
            is_admin = true;
        }
    }

    if (!is_admin) {
        dpp::message msg(
            "❌ Tu dois être administrateur du serveur pour utiliser `/setup`."
        );
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    dpp::message m(
        event.command.channel_id,
        "Configuration du bot\n"
        "Choisis ce que tu veux configurer :\n"
        "• Salons (commandes, ping, forum, logs)\n"
        "• Rôles (organisateur, ping alliance)\n"
        "• Options avancées (nb bateaux par défaut, timezone…)\n"
        "_Seuls les catégories Salons et Rôles sont obligatoires pour utiliser le bot._"
    );
    m.set_flags(dpp::m_ephemeral);

    dpp::component row;
    row.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_primary)
            .set_id("setup_channels")
            .set_label("Salons")
    );
    row.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_primary)
            .set_id("setup_roles")
            .set_label("Rôles")
    );
    row.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_secondary)
            .set_id("setup_advanced")
            .set_label("Options avancées")
    );

    m.add_component(row);

    event.reply(m);
}

