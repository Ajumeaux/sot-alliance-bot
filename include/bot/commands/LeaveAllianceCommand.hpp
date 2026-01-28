#pragma once

#include <memory>
#include <dpp/dpp.h>

#include "bot/commands/ISlashCommand.hpp"

namespace odb { namespace pgsql { class database; } }

class LeaveAllianceCommand : public ISlashCommand {
public:
    std::string subcommand_name() const override {
        return "quitter";
    }

    std::string description() const override {
        return "Quitter une alliance";
    }

    void handle(const dpp::slashcommand_t& event,
                const std::shared_ptr<odb::pgsql::database>& db) const override;
};
