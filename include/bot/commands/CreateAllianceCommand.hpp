#pragma once

#include "bot/commands/ISlashCommand.hpp"

class CreateAllianceCommand : public ISlashCommand {
public:
    std::string subcommand_name() const override {
        return "creer";
    }

    std::string description() const override {
        return "Créer une nouvelle alliance";
    }

    void handle(const dpp::slashcommand_t& event,
                const std::shared_ptr<odb::pgsql::database>& db) const override;
};
