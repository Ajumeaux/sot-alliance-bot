#include "bot/commands/JoinAllianceCommand.hpp"

#include <dpp/dpp.h>

#include "bot/ui/JoinAllianceUI.hpp"

void JoinAllianceCommand::handle(const dpp::slashcommand_t& event,
                         const std::shared_ptr<odb::pgsql::database>& db) const
{
    if (event.command.guild_id == 0) {
        dpp::message msg("❌ Cette commande doit être utilisée dans un serveur, pas en DM.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    JoinAllianceUI::open(event, db);
}
