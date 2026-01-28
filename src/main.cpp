#include <iostream>
#include <cstdlib>
#include <memory>
#include <string>

#include "util/env.hpp"
#include "db/Database.hpp"
#include "db/Schema.hpp"
#include "bot/AllianceBot.hpp"

int main() {
    std::cout.setf(std::ios::unitbuf);

    const char* token = std::getenv("DISCORD_TOKEN");
    if (!token) {
        std::cerr << "Erreur : la variable d'environnement DISCORD_TOKEN n'est pas définie.\n";
        return 1;
    }

    DbConfig cfg = load_db_config_from_env();
    auto db = make_database(cfg);

    init_schema(db);
    test_connection(db);

    AllianceBot bot(token, db);
    bot.run();

    return 0;
}
