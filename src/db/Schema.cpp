#include "db/Schema.hpp"

#include <iostream>

#include <odb/schema-catalog.hxx>
#include <odb/transaction.hxx>
#include <odb/pgsql/database.hxx>

void init_schema(const std::shared_ptr<odb::pgsql::database>& db) {
    try {
        odb::schema_version v = db->schema_version();

        if (v == 0) {
            std::cout << "[DB] Aucun schéma ODB, création...\n";
            odb::transaction t(db->begin());
            odb::schema_catalog::create_schema(*db);
            t.commit();
            std::cout << "[DB] Schéma ODB créé.\n";
        } else {
            std::cout << "[DB] Schéma ODB déjà présent (v=" << v << ")\n";
        }
    } catch (const std::exception& ex) {
        std::cerr << "[DB] Erreur init schéma : " << ex.what() << "\n";
    }
}

void test_connection(const std::shared_ptr<odb::pgsql::database>& db) {
    try {
        odb::transaction t(db->begin());
        t.commit();
        std::cout << "[DB] Connexion OK\n";
    } catch (const std::exception& ex) {
        std::cerr << "[DB] Erreur de connexion/test : " << ex.what() << "\n";
    }
}
