#include "bot/commands/LeaveAllianceCommand.hpp"

#include <dpp/dpp.h>
#include <odb/pgsql/database.hxx>

#include "bot/ui/LeaveAllianceUI.hpp"

void LeaveAllianceCommand::handle(const dpp::slashcommand_t& event,
                                  const std::shared_ptr<odb::pgsql::database>& db) const
{
    LeaveAllianceUI::open(event, db);
}
