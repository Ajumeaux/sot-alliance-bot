#include "bot/commands/CreateAllianceCommand.hpp"

#include <dpp/dpp.h>

#include <odb/pgsql/database.hxx>
#include <odb/transaction.hxx>
#include <odb/exceptions.hxx>

#include "model/bot_settings.hxx"
#include "bot_settings-odb.hxx"

#include "bot/ui/CreateAllianceUI.hpp"

void CreateAllianceCommand::handle(const dpp::slashcommand_t& event,
                                   const std::shared_ptr<odb::pgsql::database>& db) const
{
    if (event.command.guild_id == 0) {
        dpp::message msg("❌ Cette commande doit être utilisée dans un serveur, pas en DM.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    const std::uint64_t guild_id   = static_cast<std::uint64_t>(event.command.guild_id);
    const std::uint64_t channel_id = static_cast<std::uint64_t>(event.command.channel_id);

    std::uint64_t commands_channel_id = 0;

    try {
        odb::transaction t(db->begin());

        std::unique_ptr<BotSettings> settings;
        try {
            settings.reset(db->load<BotSettings>(guild_id));
        } catch (const odb::object_not_persistent&) {
            t.commit();
            dpp::message msg(
                "❌ Ce serveur n'est pas encore configuré.\n"
                "Lance d'abord `/setup` pour définir les salons et rôles."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return;
        }

        commands_channel_id = settings->command_channel_id();
        t.commit();
    }
    catch (const std::exception& ex) {
        dpp::message msg(
            std::string("❌ Erreur DB lors du chargement de la configuration : ")
            + ex.what()
        );
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    if (commands_channel_id == 0) {
        dpp::message msg(
            "❌ Aucun **salon de commandes** n'est configuré.\n"
            "Va dans `/setup` → **Salons** pour définir le salon utilisé par le bot."
        );
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    if (channel_id != commands_channel_id) {
        std::string mention = "<#" + std::to_string(commands_channel_id) + ">";
        dpp::message msg(
            "❌ La commande `/createalliance` ne peut être utilisée que dans "
            + mention + "."
        );
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    CreateAllianceUI::open_modal(event);
}
