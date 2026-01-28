#include "bot/commands/CancelAllianceCommand.hpp"

#include <dpp/dpp.h>

#include "bot/ui/CancelAllianceUI.hpp"

void CancelAllianceCommand::handle(
    const dpp::slashcommand_t& event,
    const std::shared_ptr<odb::pgsql::database>& db
) const
{
    CancelAllianceUI::open(event, db);
}
