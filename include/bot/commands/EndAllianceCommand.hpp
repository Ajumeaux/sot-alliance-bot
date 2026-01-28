#pragma once

#include <memory>
#include <dpp/dpp.h>

#include "bot/commands/ISlashCommand.hpp"

namespace odb { namespace pgsql { class database; } }

class EndAllianceCommand : public ISlashCommand {
public:
    std::string subcommand_name() const override {
        return "terminer";
    }

    std::string description() const override {
        return "Clôturer une alliance en cours";
    }

    void handle(const dpp::slashcommand_t& event,
                const std::shared_ptr<odb::pgsql::database>& db) const override;
};
