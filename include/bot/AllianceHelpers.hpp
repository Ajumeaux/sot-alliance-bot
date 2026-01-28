#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <ctime>

#include <dpp/dpp.h>

#include <odb/pgsql/database.hxx>

#include "model/alliances.hxx"
#include "alliances-odb.hxx"

#include "model/ships.hxx"
#include "ships-odb.hxx"

#include "model/alliance_participants.hxx"
#include "alliance_participants-odb.hxx"

#include "model/alliance_discord_objects.hxx"
#include "alliance_discord_objects-odb.hxx"

namespace alliance_helpers {

constexpr uint32_t ALLIANCE_GOLD_COLOR = 0xFFCF40;

struct AllianceRosterData {
    Alliance alliance;
    std::vector<Ship> ships;
    std::unordered_map<std::uint64_t, std::vector<AllianceParticipant>> by_ship;
};

std::string trim(const std::string& s);
bool parse_time_to_hhmm(const std::string& input, std::string& out);
bool parse_iso_date(const std::string& iso,
                    int& year, int& month, int& day);
bool make_time_t(const std::string& date_iso,
                 const std::string& time_str,
                 std::time_t& out);
bool parse_french_date_to_iso(const std::string& input,
                              std::string& iso_out);
bool parse_date_ddmm(const std::string& input,
                     const std::tm& base_tm,
                     std::tm& out_tm);
std::string random_alliance_name();

// Helpers date / texte
std::string french_day_name(const std::tm& tm);
std::string format_hhmm(std::time_t t);

// Helpers bateaux (version DB : HullType)
std::string hull_label(HullType h);
int hull_capacity(HullType h);

// Chargement complet de la flotte + participants
AllianceRosterData load_alliance_roster_data(
    const std::shared_ptr<odb::pgsql::database>& db,
    std::uint64_t alliance_id
);

// Construction des embeds dorés
std::vector<dpp::embed> build_alliance_embeds(
    const AllianceRosterData& data
);

// Création / MAJ du message de roster dans le thread
void create_or_update_alliance_roster_message(
    dpp::cluster* cluster,
    const std::shared_ptr<odb::pgsql::database>& db,
    std::uint64_t alliance_id,
    dpp::snowflake thread_id
);

} // namespace alliance_helpers
