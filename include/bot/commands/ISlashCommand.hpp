#pragma once

#include <memory>
#include <string>

#include <dpp/dpp.h>

namespace odb { namespace pgsql {
    class database;
}}

class ISlashCommand {
public:
    virtual ~ISlashCommand() = default;

    virtual std::string subcommand_name() const = 0;

    virtual std::string description() const = 0;

    virtual void build_subcommand(dpp::command_option& opt) const {
        opt.type = dpp::co_sub_command;
        opt.name = subcommand_name();
        opt.description = description();
    }

    virtual void handle(const dpp::slashcommand_t& event,
                        const std::shared_ptr<odb::pgsql::database>& db) const = 0;
};

