#include "bot/AllianceHelpers.hpp"

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iostream>
#include <cctype>
#include <ctime>
#include <vector>
#include <cstdlib>
#include <cstdio>


#include <odb/transaction.hxx>
#include <odb/query.hxx>

namespace alliance_helpers {

std::string trim(const std::string& s) {
    std::size_t b = 0;
    while (b < s.size() &&
           std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }

    std::size_t e = s.size();
    while (e > b &&
           std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }

    return s.substr(b, e - b);
}

bool parse_time_to_hhmm(const std::string& input, std::string& out)
{
    std::string s;
    s.reserve(input.size());
    for (char c : input) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lc == 'h')
                lc = ':'; // 7h30 -> 7:30, 7h -> 7:
            s.push_back(lc);
        }
    }

    if (s.empty())
        return false;

    int h = 0;
    int m = 0;

    auto pos = s.find(':');
    if (pos == std::string::npos) {
        // "7", "07" -> 7:00
        for (char c : s) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return false;
            }
        }

        try {
            h = std::stoi(s);
        } catch (...) {
            return false;
        }
        m = 0;
    } else {
        std::string h_str = s.substr(0, pos);
        std::string m_str;

        if (pos + 1 < s.size()) {
            m_str = s.substr(pos + 1);
        } else {
            m_str = "0"; // "7:" -> minutes = 0
        }

        if (h_str.empty())
            return false;

        try {
            h = std::stoi(h_str);
            m = std::stoi(m_str);
        } catch (...) {
            return false;
        }
    }

    if (h < 0 || h > 23 || m < 0 || m > 59)
        return false;

    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << h
        << ":"
        << std::setw(2) << std::setfill('0') << m;

    out = oss.str();
    return true;
}

bool parse_iso_date(const std::string& iso,
                    int& year, int& month, int& day)
{
    std::string s = trim(iso);
    auto p1 = s.find('-');
    if (p1 == std::string::npos) return false;
    auto p2 = s.find('-', p1 + 1);
    if (p2 == std::string::npos) return false;

    try {
        year  = std::stoi(s.substr(0, p1));
        month = std::stoi(s.substr(p1 + 1, p2 - p1 - 1));
        day   = std::stoi(s.substr(p2 + 1));
    } catch (...) {
        return false;
    }

    if (month < 1 || month > 12 ||
        day   < 1 || day   > 31)
        return false;

    return true;
}

bool make_time_t(const std::string& date_iso,
                 const std::string& time_str,
                 std::time_t& out)
{
    int year = 0, month = 0, day = 0;
    if (!parse_iso_date(date_iso, year, month, day))
        return false;

    std::string s;
    s.reserve(time_str.size());
    for (char c : time_str) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            s.push_back(c);
        }
    }
    auto pos = s.find(':');
    if (pos == std::string::npos)
        return false;

    int hour = 0, minute = 0;
    try {
        hour   = std::stoi(s.substr(0, pos));
        minute = std::stoi(s.substr(pos + 1));
    } catch (...) {
        return false;
    }

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
        return false;

    std::tm tm {};
    tm.tm_year = year - 1900;
    tm.tm_mon  = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min  = minute;
    tm.tm_sec  = 0;

    out = std::mktime(&tm);
    return true;
}

bool parse_french_date_to_iso(const std::string& input,
                              std::string& iso_out)
{
    std::string s = trim(input);
    if (s.empty()) return false;

    auto p1 = s.find('/');
    if (p1 == std::string::npos) return false;
    auto p2 = s.find('/', p1 + 1);

    int day = 0, month = 0, year = 0;

    try {
        day = std::stoi(s.substr(0, p1));

        if (p2 == std::string::npos) {
            // JJ/MM -> année courante
            month = std::stoi(s.substr(p1 + 1));
            std::time_t now = std::time(nullptr);
            std::tm tm_now {};
#ifdef _WIN32
            localtime_s(&tm_now, &now);
#else
            tm_now = *std::localtime(&now);
#endif
            year = tm_now.tm_year + 1900;
        } else {
            // JJ/MM/AAAA, JJ/MM/YY
            month = std::stoi(s.substr(p1 + 1, p2 - p1 - 1));
            std::string ystr = s.substr(p2 + 1);
            year = std::stoi(ystr);
            if (year < 100) {
                year += 2000;
            }
        }
    } catch (...) {
        return false;
    }

    if (month < 1 || month > 12 || day < 1 || day > 31)
        return false;

    std::tm tm {};
    tm.tm_year = year - 1900;
    tm.tm_mon  = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 12;
    tm.tm_min  = 0;
    tm.tm_sec  = 0;
    std::time_t t = std::mktime(&tm);
    if (t == -1)
        return false;
    if (tm.tm_year != year - 1900 ||
        tm.tm_mon  != month - 1   ||
        tm.tm_mday != day)
        return false;

    char buf[20];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    iso_out = buf;
    return true;
}

bool parse_date_ddmm(const std::string& input,
                     const std::tm& base_tm,
                     std::tm& out_tm)
{
    std::string s = trim(input);
    if (s.empty())
        return false;

    int d = 0, m = 0, y = 0;

    std::size_t p1 = s.find('/');
    if (p1 == std::string::npos)
        return false;

    std::size_t p2 = s.find('/', p1 + 1);

    try {
        d = std::stoi(s.substr(0, p1));
        if (p2 == std::string::npos) {
            // dd/mm
            m = std::stoi(s.substr(p1 + 1));
            y = base_tm.tm_year + 1900;
        } else {
            // dd/mm/yyyy
            m = std::stoi(s.substr(p1 + 1, p2 - (p1 + 1)));
            y = std::stoi(s.substr(p2 + 1));
        }
    } catch (...) {
        return false;
    }

    if (d <= 0 || d > 31 || m <= 0 || m > 12 || y < 1970 || y > 2100)
        return false;

    out_tm = base_tm;
    out_tm.tm_mday = d;
    out_tm.tm_mon  = m - 1;
    out_tm.tm_year = y - 1900;

    return true;
}

std::string random_alliance_name() {
    static const std::vector<std::string> names = {
        "Alliance des Sept Mers",
        "Alliance des Damnés",
        "Alliance des Flibustiers",
        "Alliance des Crânes Dorés",
        "Alliance des Grogophiles",
        "Alliance des Vieux Loups de Mer",
        "Alliance des Sans-Pavillon",
        "Alliance du Vent du Nord",
        "Alliance des No-Life",
        "Alliance des Sans-Savon",
        "Alliance des TBM"
    };
    static bool seeded = false;
    if (!seeded) {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
        seeded = true;
    }
    return names[std::rand() % names.size()];
}


std::string french_day_name(const std::tm& tm) {
    static const char* days[] = {
        "Dimanche", "Lundi", "Mardi", "Mercredi",
        "Jeudi", "Vendredi", "Samedi"
    };
    int idx = tm.tm_wday;
    if (idx < 0 || idx > 6) idx = 0;
    return days[idx];
}

std::string format_hhmm(std::time_t t) {
    std::tm tm {};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    tm = *std::localtime(&t);
#endif
    std::ostringstream oss;
    oss << tm.tm_hour << 'h'
        << std::setw(2) << std::setfill('0') << tm.tm_min;
    return oss.str();
}

std::string hull_label(HullType h) {
    switch (h) {
        case HullType::sloop:   return "Sloop";
        case HullType::brig:    return "Brigantin";
        case HullType::galleon: return "Galion";
    }
    return "Brigantin";
}

int hull_capacity(HullType h) {
    switch (h) {
        case HullType::sloop:   return 2;
        case HullType::brig:    return 3;
        case HullType::galleon: return 4;
    }
    return 3;
}

AllianceRosterData load_alliance_roster_data(
    const std::shared_ptr<odb::pgsql::database>& db,
    std::uint64_t alliance_id
)
{
    using AllianceQuery = odb::query<Alliance>;
    using ShipQuery     = odb::query<Ship>;
    using PartQuery     = odb::query<AllianceParticipant>;

    AllianceRosterData data;

    odb::transaction t(db->begin());

    {
        std::unique_ptr<Alliance> a(db->load<Alliance>(alliance_id));
        data.alliance = *a;
    }

    {
        using ShipResult = odb::result<Ship>;
        ShipQuery q(ShipQuery::alliance_id == alliance_id);
        ShipResult sres(
            db->query<Ship>(q + " ORDER BY " + ShipQuery::slot)
        );

        for (const Ship& s : sres) {
            data.ships.push_back(s);
        }
    }

    {
        using PartResult = odb::result<AllianceParticipant>;
        PartResult pres(
            db->query<AllianceParticipant>(
                PartQuery::alliance_id == alliance_id &&
                PartQuery::left_at == 0
            )
        );

        for (const AllianceParticipant& p : pres) {
            data.by_ship[p.ship_id()].push_back(p);
        }
    }

    t.commit();
    return data;
}

std::vector<dpp::embed> build_alliance_embeds(
    const AllianceRosterData& data
)
{
    const Alliance& alliance = data.alliance;
    const auto& ships        = data.ships;
    const auto& by_ship      = data.by_ship;

    std::time_t scheduled_at = alliance.scheduled_at();
    std::time_t sale_at      = alliance.sale_at();

    std::tm tm_start {};
#ifdef _WIN32
    localtime_s(&tm_start, &scheduled_at);
#else
    tm_start = *std::localtime(&scheduled_at);
#endif

    std::string day_name  = french_day_name(tm_start);
    std::string start_str = format_hhmm(scheduled_at);
    std::string sale_str  = format_hhmm(sale_at);

    int day   = tm_start.tm_mday;
    int month = tm_start.tm_mon + 1;

    std::time_t replace_at = scheduled_at + 30 * 60;
    std::string replace_str = format_hhmm(replace_at);

    std::time_t rdv1 = sale_at - 30 * 60;
    std::time_t rdv2 = sale_at - 15 * 60;
    std::string rdv1_str = format_hhmm(rdv1);
    std::string rdv2_str = format_hhmm(rdv2);

    std::vector<dpp::embed> embeds;

    dpp::embed e;
    e.set_color(ALLIANCE_GOLD_COLOR);
    e.set_title("🏴‍☠️ Alliance programmée");

    {
        std::ostringstream desc;
        desc << "\n";
        desc << "**Alliance** : **" << alliance.name() << "**\n"
             << "**Jour/heure** : *" << day_name << " "
             << std::setw(2) << std::setfill('0') << day
             << "/"
             << std::setw(2) << std::setfill('0') << month
             << "* de " << start_str << " à " << sale_str << "\n";

        std::string organizer_mention =
            "<@" + std::to_string(alliance.organizer_id()) + ">";

        desc << "**Organisateur** : " << organizer_mention << "\n";

        const std::string& bras_droit = alliance.right_hand();
        desc << "**Bras droit** : "
             << (bras_droit.empty() ? "_non défini_" : bras_droit) << "\n";

        desc << "**Reprise des bateaux** : "
             << (alliance.ships_reuse_planned() ? "✅ Prévu" : "❌ Non prévu") << "\n";

        e.set_description(desc.str());
    }

    {
        std::ostringstream der;
        der << "\n";
        der << "- Début des try à **" << start_str
            << "**, remplacement des retardataires à **" << replace_str << "**\n"
            << "- RDV vers **" << rdv1_str << "-" << rdv2_str
            << "** pour vendre à **" << sale_str << "**";

        e.add_field("📜 Déroulement", der.str(), false);
    }

    if (ships.empty()) {
        e.add_field(
            "🚢 FLOTTE",
            "\n_Aucun bateau configuré pour cette alliance._",
            false
        );
    } else {
        e.add_field(
            "🚢 FLOTTE",
            "\u200b",
            false
        );

        for (const Ship& ship : ships) {
            std::string hull = hull_label(ship.hull_type());
            std::string role = ship.crew_role().empty()
                             ? "Libre"
                             : ship.crew_role();

            int cap = hull_capacity(ship.hull_type());

            std::vector<AllianceParticipant> participants;
            auto it = by_ship.find(ship.id());
            if (it != by_ship.end()) {
                participants = it->second;
                std::sort(participants.begin(), participants.end(),
                          [](const AllianceParticipant& a,
                             const AllianceParticipant& b) {
                              return a.joined_at() < b.joined_at();
                          });
            }

            std::ostringstream value;
            int nb = static_cast<int>(participants.size());

            // Slots principaux
            for (int i = 0; i < cap; ++i) {
                if (i < nb) {
                    value << "• <@" << participants[i].user_id() << ">\n";
                } else {
                    value << "• _dispo_\n";
                }
            }

            // Remplaçants : italique, plus discret, pas de ligne vide avant,
            // pas de double saut de ligne entre bateaux.
            if (nb > cap) {
                value << "*Remplaçants :*\n";
                for (int i = cap; i < nb; ++i) {
                    value << "• <@" << participants[i].user_id() << ">\n";
                }
            } else {
                value << "*Remplaçants :* _aucun_\n";
            }

            std::ostringstream field_name;
            field_name << hull << " - " << role
                       << " [" << std::min(nb, cap) << "/" << cap << "]";

            e.add_field(field_name.str(), value.str(), false);
        }
    }

    embeds.push_back(e);
    return embeds;
}

void create_or_update_alliance_roster_message(
    dpp::cluster* cluster,
    const std::shared_ptr<odb::pgsql::database>& db,
    std::uint64_t alliance_id,
    dpp::snowflake thread_id
)
{
    if (!cluster) return;

    AllianceRosterData data = load_alliance_roster_data(db, alliance_id);
    auto embeds = build_alliance_embeds(data);

    using ObjQuery  = odb::query<AllianceDiscordObject>;
    using ObjResult = odb::result<AllianceDiscordObject>;

    std::unique_ptr<AllianceDiscordObject> roster_obj;

    {
        odb::transaction t(db->begin());
        ObjResult r = db->query<AllianceDiscordObject>(
            ObjQuery::alliance_id == alliance_id &&
            ObjQuery::type == DiscordObjectType::message
        );

        auto it = r.begin();
        if (it != r.end()) {
            roster_obj = std::make_unique<AllianceDiscordObject>(*it);
        }
        t.commit();
    }

    if (!roster_obj) {
        dpp::message msg;
        msg.channel_id = thread_id;
        msg.set_flags(0);
        msg.embeds.clear();
        for (auto& e : embeds) {
            msg.add_embed(e);
        }

        cluster->message_create(
            msg,
            [db, alliance_id](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    std::cerr << "[Alliance] Erreur création message flotte (embed): "
                              << cb.get_error().message << "\n";
                    return;
                }

                dpp::message created = cb.get<dpp::message>();
                dpp::snowflake msg_id = created.id;

                try {
                    odb::transaction t2(db->begin());
                    AllianceDiscordObject msg_obj(
                        alliance_id,
                        DiscordObjectType::message,
                        static_cast<std::uint64_t>(msg_id),
                        "Message principal de l'alliance (embed)",
                        false
                    );
                    db->persist(msg_obj);
                    t2.commit();
                } catch (const std::exception& ex) {
                    std::cerr << "[Alliance] Erreur DB enregistrement message flotte : "
                              << ex.what() << "\n";
                }
            }
        );
    } else {
        dpp::message msg;
        msg.id         = static_cast<dpp::snowflake>(roster_obj->discord_id());
        msg.channel_id = thread_id;
        msg.embeds.clear();
        for (auto& e : embeds) {
            msg.add_embed(e);
        }

        cluster->message_edit(
            msg,
            [](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    std::cerr << "[Alliance] Erreur édition message flotte (embed): "
                              << cb.get_error().message << "\n";
                }
            }
        );
    }
}

} // namespace alliance_helpers
