#include "bot/ui/CreateAllianceUI.hpp"

#include <ctime>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <iostream>
#include <cctype>
#include <vector>
#include <cstdio>
#include <cstdlib>

#include <dpp/dpp.h>

#include <odb/pgsql/database.hxx>
#include <odb/transaction.hxx>
#include <odb/exceptions.hxx>

#include "model/bot_settings.hxx"
#include "bot_settings-odb.hxx"

#include "model/alliances.hxx"
#include "alliances-odb.hxx"

#include "model/ships.hxx"
#include "ships-odb.hxx"

#include "model/alliance_discord_objects.hxx"
#include "alliance_discord_objects-odb.hxx"

#include "bot/AllianceHelpers.hpp"

namespace {

using alliance_helpers::trim;
using alliance_helpers::parse_time_to_hhmm;
using alliance_helpers::parse_iso_date;
using alliance_helpers::make_time_t;
using alliance_helpers::parse_french_date_to_iso;
using alliance_helpers::random_alliance_name;

enum class ShipHull {
    Sloop,
    Brig,
    Galleon
};

struct ShipConfig {
    ShipHull hull = ShipHull::Brig;
    std::string role;
    bool has_hull = false;
    bool has_role = false;
};

static void ack_select(const dpp::select_click_t& event)
{
    event.reply();
}



struct PendingAllianceKey {
    std::uint64_t guild_id;
    std::uint64_t user_id;

    bool operator==(const PendingAllianceKey&) const = default;
};

struct PendingAllianceKeyHash {
    std::size_t operator()(const PendingAllianceKey& k) const noexcept {
        std::size_t h1 = std::hash<std::uint64_t>{}(k.guild_id);
        std::size_t h2 = std::hash<std::uint64_t>{}(k.user_id);
        return h1 ^ (h2 << 1);
    }
};

struct PendingAlliance {
    std::string date_iso;    // "2025-11-18"
    std::string start_time;  // "07:30"
    std::string sale_time;   // "18:00"
    dpp::snowflake bras_droit_id = 0;

    bool reprise = false;
    bool reprise_set = false;

    unsigned short max_ships = 0;
    std::vector<ShipConfig> ships;
    std::size_t current_ship = 0;
    bool fleet_config_started = false;

    bool ready_basic() const {
        return !date_iso.empty() &&
               !start_time.empty() &&
               !sale_time.empty();
    }

    bool fleet_ready() const {
        if (!fleet_config_started || max_ships == 0 || ships.size() != max_ships)
            return false;
        for (const auto& s : ships) {
            if (!s.has_hull || !s.has_role)
                return false;
        }
        return true;
    }

    bool ready() const {
        return ready_basic() && fleet_ready() && reprise_set;
    }
};

std::unordered_map<PendingAllianceKey,
                   PendingAlliance,
                   PendingAllianceKeyHash> pending_alliances;

PendingAlliance& get_state(dpp::snowflake guild_id,
                           dpp::snowflake user_id)
{
    PendingAllianceKey key{
        static_cast<std::uint64_t>(guild_id),
        static_cast<std::uint64_t>(user_id)
    };
    return pending_alliances[key];
}

void clear_state(dpp::snowflake guild_id,
                 dpp::snowflake user_id)
{
    PendingAllianceKey key{
        static_cast<std::uint64_t>(guild_id),
        static_cast<std::uint64_t>(user_id)
    };
    pending_alliances.erase(key);
}

static void send_ship_config_prompt(const dpp::button_click_t& event,
                                    dpp::snowflake /*guild_id*/,
                                    dpp::snowflake /*user_id*/,
                                    const PendingAlliance& state)
{
    if (state.ships.empty() || state.current_ship >= state.ships.size())
        return;

    const std::size_t idx   = state.current_ship;
    const std::size_t total = state.ships.size();

    std::ostringstream content;
    content << "⚓ Configuration du **bateau " << (idx + 1) << "/" << total << "**\n"
            << "Choisis la **coque** et le **rôle** pour ce navire.\n"
            << "Tu peux aussi choisir `Autre…` pour définir un rôle personnalisé.";


    dpp::message m;
    m.set_content(content.str());
    m.set_flags(dpp::m_ephemeral);

    dpp::component hull_select;
    hull_select.set_type(dpp::cot_selectmenu)
               .set_id("create_alliance_ship_hull")
               .set_placeholder("Type de navire")
               .set_min_values(1)
               .set_max_values(1);
    hull_select.add_select_option(dpp::select_option("Sloop", "sloop"));
    hull_select.add_select_option(dpp::select_option("Brigantin", "brig"));
    hull_select.add_select_option(dpp::select_option("Galion", "galleon"));

    m.add_component(dpp::component().add_component(hull_select));

    dpp::component role_select;
    role_select.set_type(dpp::cot_selectmenu)
               .set_id("create_alliance_ship_role")
               .set_placeholder("Rôle du navire")
               .set_min_values(1)
               .set_max_values(1);
    role_select.add_select_option(dpp::select_option("FDD", "FDD"));
    role_select.add_select_option(dpp::select_option("Event", "Event"));
    role_select.add_select_option(dpp::select_option("Athéna", "Athéna"));
    role_select.add_select_option(dpp::select_option("Chasseur", "Chasseur"));
    role_select.add_select_option(dpp::select_option("Libre", "Libre"));
    role_select.add_select_option(dpp::select_option("Autre…", "custom"));

    m.add_component(dpp::component().add_component(role_select));

    dpp::component row_buttons;

    if (idx + 1 == total) {
        row_buttons.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_id("create_alliance_ship_finish")
                .set_label("Terminer et créer l'alliance")
                .set_style(dpp::cos_primary)
        );
    } else {
        row_buttons.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_id("create_alliance_ship_next")
                .set_label("Bateau suivant")
                .set_style(dpp::cos_primary)
        );
    }

    m.add_component(row_buttons);

    event.reply(m);
}

} // namespace

void CreateAllianceUI::open_modal(const dpp::slashcommand_t& event) {
    if (event.command.guild_id == 0) {
        dpp::message msg("❌ Cette commande doit être utilisée dans un serveur, pas en DM.");
        msg.set_flags(dpp::m_ephemeral);
        event.reply(msg);
        return;
    }

    dpp::snowflake guild_id = event.command.guild_id;
    dpp::snowflake user_id  = event.command.usr.id;

    clear_state(guild_id, user_id);
    get_state(guild_id, user_id);

    dpp::message m(
        event.command.channel_id,
        "Configuration de l'alliance :\n"
        "1️⃣ Choisis la **date** (ou le bouton *Saisir la date et heures* pour une date plus éloignée)\n"
        "2️⃣ Choisis l'**heure de début**\n"
        "3️⃣ Choisis l'**heure de vente**\n"
        "4️⃣ Configure la **flotte** (bouton *Configurer la flotte*)\n"
        "5️⃣ (Optionnel) règle le **bras droit** et la **reprise des bateaux** dans le second message."
    );

    m.set_flags(dpp::m_ephemeral);

    std::time_t now = std::time(nullptr);
    std::tm local_now {};
#ifdef _WIN32
    localtime_s(&local_now, &now);
#else
    local_now = *std::localtime(&now);
#endif

    const bool is_summer_time = (local_now.tm_isdst > 0);

    // hiver : 18h / 2h
    // été   : 19h / 3h
    int evening_start_hour      = is_summer_time ? 19 : 18;
    int gold_rush_evening_hour  = is_summer_time ? 19 : 18;
    int gold_rush_late_hour     = is_summer_time ? 3  : 2;

    auto hhmm = [](int hour) {
        std::ostringstream oss;
        oss << std::setw(2) << std::setfill('0') << hour << ":00";
        return oss.str();
    };

    dpp::component date_select;
    date_select.set_type(dpp::cot_selectmenu)
               .set_id("create_alliance_date")
               .set_placeholder("Choisis le jour de l'alliance")
               .set_min_values(1)
               .set_max_values(1);

    for (int i = 0; i < 21; ++i) {
        std::time_t day_ts = now + i * 24 * 3600;
        std::tm tm_day {};
#ifdef _WIN32
        localtime_s(&tm_day, &day_ts);
#else
        tm_day = *std::localtime(&day_ts);
#endif

        char value[32];
        std::strftime(value, sizeof(value), "%Y-%m-%d", &tm_day);

        int day   = tm_day.tm_mday;
        int month = tm_day.tm_mon + 1;

        std::ostringstream label;
        label << alliance_helpers::french_day_name(tm_day) << " "
              << std::setw(2) << std::setfill('0') << day
              << "/"
              << std::setw(2) << std::setfill('0') << month;

        date_select.add_select_option(
            dpp::select_option(label.str(), value)
        );
    }

    m.add_component(
        dpp::component().add_component(date_select)
    );

    dpp::component start_select;
    start_select.set_type(dpp::cot_selectmenu)
                .set_id("create_alliance_start")
                .set_placeholder("Heure de début")
                .set_min_values(1)
                .set_max_values(1);

    start_select.add_select_option(
        dpp::select_option("7h30", "07:30")
    );

    {
        std::ostringstream label_soir;
        label_soir << evening_start_hour << "h00";
        start_select.add_select_option(
            dpp::select_option(label_soir.str(), hhmm(evening_start_hour))
        );
    }

    m.add_component(
        dpp::component().add_component(start_select)
    );

    dpp::component sale_select;
    sale_select.set_type(dpp::cot_selectmenu)
               .set_id("create_alliance_sale")
               .set_placeholder("Heure de vente")
               .set_min_values(1)
               .set_max_values(1);

    {
        std::ostringstream label_evening;
        label_evening << std::setw(2) << std::setfill('0')
                      << gold_rush_evening_hour
                      << "h00 (Gold Rush soir)";
        sale_select.add_select_option(
            dpp::select_option(label_evening.str(),
                               hhmm(gold_rush_evening_hour))
        );
    }

    {
        std::ostringstream label_late;
        label_late << std::setw(2) << std::setfill('0')
                   << gold_rush_late_hour
                   << "h00 (Gold Rush nuit)";
        sale_select.add_select_option(
            dpp::select_option(label_late.str(),
                               hhmm(gold_rush_late_hour))
        );
    }

    m.add_component(
        dpp::component().add_component(sale_select)
    );

    dpp::component row_buttons;
    row_buttons.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_id("create_alliance_datetime_manual")
            .set_label("Saisir date & heures")
            .set_style(dpp::cos_secondary)
    );
    row_buttons.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_id("create_alliance_configure_fleet")
            .set_label("Configurer la flotte")
            .set_style(dpp::cos_primary)
    );

    m.add_component(row_buttons);

    event.reply(m);

    dpp::cluster* cluster = event.from()->creator;
    if (cluster) {
        dpp::message m2(
            event.command.channel_id,
            "Options facultatives de l'alliance :\n"
        );
        m2.set_flags(dpp::m_ephemeral);

        dpp::component bras_select;
        bras_select.set_type(dpp::cot_user_selectmenu)
                   .set_id("create_alliance_brasdroit")
                   .set_placeholder("Choisis un bras droit")
                   .set_min_values(0)
                   .set_max_values(1);

        dpp::component reprise_select;
        reprise_select.set_type(dpp::cot_selectmenu)
                      .set_id("create_alliance_reuse_ships")
                      .set_placeholder("Reprise des bateaux ?")
                      .set_min_values(1)
                      .set_max_values(1);
        reprise_select.add_select_option(dpp::select_option("Reprise prévue", "yes"));
        reprise_select.add_select_option(dpp::select_option("Pas de reprise", "no"));

        m2.add_component(dpp::component().add_component(bras_select));
        m2.add_component(dpp::component().add_component(reprise_select));

        cluster->interaction_followup_create(event.command.token, m2);
    }
}

bool CreateAllianceUI::handle_modal(const dpp::form_submit_t& event,
                                    const std::shared_ptr<odb::pgsql::database>& /*db*/) const
{
    if (event.command.guild_id == 0)
        return false;

    if (event.custom_id == "create_alliance_datetime_modal") {
        dpp::snowflake guild_id = event.command.guild_id;
        dpp::snowflake user_id  = event.command.usr.id;

        std::string date_input  = trim(get_text_field(event, 0, 0));
        std::string start_input = trim(get_text_field(event, 1, 0));
        std::string sale_input  = trim(get_text_field(event, 2, 0));

        if (date_input.empty() || start_input.empty() || sale_input.empty()) {
            reply_ephemeral(
                event,
                "❌ Merci de renseigner **date, heure de début et heure de vente**.\n"
                "Exemple : `15/11`, `07:30`, `18:00`."
            );
            return true;
        }

        std::string iso;
        if (!parse_french_date_to_iso(date_input, iso)) {
            reply_ephemeral(
                event,
                "❌ Je n'ai pas compris la date. Essaie par exemple `15/11` ou `15/11/2025`."
            );
            return true;
        }

        std::string start_hhmm;
        std::string sale_hhmm;

        if (!parse_time_to_hhmm(start_input, start_hhmm)) {
            reply_ephemeral(
                event,
                "❌ Je n'ai pas compris l'heure de début. Essaie par exemple `7h30` ou `07:30`."
            );
            return true;
        }

        if (!parse_time_to_hhmm(sale_input, sale_hhmm)) {
            reply_ephemeral(
                event,
                "❌ Je n'ai pas compris l'heure de vente. Essaie par exemple `18h00` ou `18:00`."
            );
            return true;
        }

        std::time_t t_start = 0;
        std::time_t t_sale  = 0;
        if (!make_time_t(iso, start_hhmm, t_start) ||
            !make_time_t(iso, sale_hhmm,  t_sale)) {
            reply_ephemeral(
                event,
                "❌ Impossible d'interpréter la date/heure, vérifie les valeurs."
            );
            return true;
        }

        PendingAlliance& state = get_state(guild_id, user_id);
        state.date_iso   = iso;
        state.start_time = start_hhmm;
        state.sale_time  = sale_hhmm;

        int year = 0, month = 0, day = 0;
        parse_iso_date(iso, year, month, day);

        std::tm tm_day {};
        tm_day.tm_year = year - 1900;
        tm_day.tm_mon  = month - 1;
        tm_day.tm_mday = day;
        std::mktime(&tm_day);

        std::ostringstream oss;
        oss << "✅ Date et heures mises à jour : **"
            << alliance_helpers::french_day_name(tm_day) << " "
            << std::setw(2) << std::setfill('0') << day
            << "/"
            << std::setw(2) << std::setfill('0') << month
            << "/" << year
            << "** de " << start_hhmm << " à " << sale_hhmm << ".";

        reply_ephemeral(event, oss.str());
        return true;
    }

    if (event.custom_id == "create_alliance_ship_role_custom_modal") {
        dpp::snowflake guild_id = event.command.guild_id;
        dpp::snowflake user_id  = event.command.usr.id;

        PendingAlliance& state = get_state(guild_id, user_id);
        if (!state.fleet_config_started || state.ships.empty()) {
            reply_ephemeral(
                event,
                "❌ La configuration de flotte a expiré ou n'est plus valide.\n"
                "Relance `/create_alliance` puis clique sur **Configurer la flotte**."
            );
            return true;
        }

        std::string role;
        try {
            role = trim(get_text_field(event, 0, 0));
        } catch (const std::exception& ex) {
            std::cerr << "[CreateAllianceUI] Exception dans ship_role_custom_modal : "
                      << ex.what() << "\n";
            reply_ephemeral(
                event,
                "❌ Impossible de lire le rôle saisi. Réessaie."
            );
            return true;
        }

        if (role.empty()) {
            reply_ephemeral(event, "❌ Merci de saisir un nom de rôle pour le bateau.");
            return true;
        }

        if (state.current_ship >= state.ships.size())
            state.current_ship = state.ships.size() - 1;

        ShipConfig& sc = state.ships[state.current_ship];
        sc.role = role;
        sc.has_role = true;

        reply_ephemeral(
            event,
            "✅ Rôle personnalisé défini : **" + role + "**."
        );
        return true;
    }

    return false;
}

bool CreateAllianceUI::handle_select(const dpp::select_click_t& event,
                                     const std::shared_ptr<odb::pgsql::database>& /*db*/)
{
    const std::string& id = event.custom_id;

    if (event.command.guild_id == 0)
        return false;

    if (event.values.empty()) {
        ack_select(event);
        return true;
    }

    dpp::snowflake guild_id = event.command.guild_id;
    dpp::snowflake user_id  = event.command.usr.id;

    PendingAlliance& state = get_state(guild_id, user_id);
    const std::string& value = event.values[0];

    if (id == "create_alliance_date") {
        state.date_iso = value;
        ack_select(event);
        return true;
    }
    else if (id == "create_alliance_start") {
        state.start_time = value;
        ack_select(event);
        return true;
    }
    else if (id == "create_alliance_sale") {
        state.sale_time = value;
        ack_select(event);
        return true;
    }
    else if (id == "create_alliance_brasdroit") {
        try {
            std::uint64_t uid = std::stoull(value);
            state.bras_droit_id = static_cast<dpp::snowflake>(uid);
        } catch (...) {
            state.bras_droit_id = 0;
        }
        ack_select(event);
        return true;
    }
    else if (id == "create_alliance_ship_hull") {
        if (!state.fleet_config_started || state.ships.empty()) {
            ack_select(event);
            return true;
        }

        ShipConfig& sc = state.ships[state.current_ship];

        if (value == "sloop") {
            sc.hull = ShipHull::Sloop;
        } else if (value == "brig") {
            sc.hull = ShipHull::Brig;
        } else if (value == "galleon") {
            sc.hull = ShipHull::Galleon;
        }
        sc.has_hull = true;

        ack_select(event);
        return true;
    }
    else if (id == "create_alliance_ship_role") {
        if (!state.fleet_config_started || state.ships.empty()) {
            ack_select(event);
            return true;
        }

        ShipConfig& sc = state.ships[state.current_ship];

        if (value == "custom") {
            dpp::interaction_modal_response modal(
                "create_alliance_ship_role_custom_modal",
                "Rôle personnalisé du bateau"
            );

            modal.add_component(
                dpp::component()
                    .set_label("Nom du rôle (ex: Rapier Cay, Reaper...)")
                    .set_id("field_ship_role_custom")
                    .set_type(dpp::cot_text)
                    .set_placeholder("Ex: Reaper")
                    .set_min_length(1)
                    .set_max_length(50)
                    .set_text_style(dpp::text_short)
            );

            event.dialog(modal);
            return true;
        } else {
            sc.role = value;
            sc.has_role = true;
            ack_select(event);
            return true;
        }
    }
    else if (id == "create_alliance_reuse_ships") {
        if (value == "yes") {
            state.reprise = true;
        } else {
            state.reprise = false;
        }
        state.reprise_set = true;
        ack_select(event);
        return true;
    }

    return false;
}

bool CreateAllianceUI::handle_button(const dpp::button_click_t& event,
                                     const std::shared_ptr<odb::pgsql::database>& db)
{
    const std::string& id = event.custom_id;

    if (event.command.guild_id == 0)
        return false;

    dpp::snowflake guild_id = event.command.guild_id;
    dpp::snowflake user_id  = event.command.usr.id;

    if (id == "create_alliance_datetime_manual") {
        dpp::interaction_modal_response modal(
            "create_alliance_datetime_modal",
            "Date & heures de l'alliance"
        );

        modal.add_component(
            dpp::component()
                .set_label("Date (JJ/MM ou JJ/MM/AAAA)")
                .set_id("field_date")
                .set_type(dpp::cot_text)
                .set_placeholder("15/11 ou 15/11/2025")
                .set_min_length(1)
                .set_max_length(20)
                .set_text_style(dpp::text_short)
        );

        modal.add_row();
        modal.add_component(
            dpp::component()
                .set_label("Heure de début (ex: 7h30 ou 07:30)")
                .set_id("field_start_time")
                .set_type(dpp::cot_text)
                .set_placeholder("07:30")
                .set_min_length(1)
                .set_max_length(10)
                .set_text_style(dpp::text_short)
        );

        modal.add_row();
        modal.add_component(
            dpp::component()
                .set_label("Heure de vente (ex: 18h00 ou 18:00)")
                .set_id("field_sale_time")
                .set_type(dpp::cot_text)
                .set_placeholder("18:00")
                .set_min_length(1)
                .set_max_length(10)
                .set_text_style(dpp::text_short)
        );

        event.dialog(modal);
        return true;
    }

    if (id == "create_alliance_configure_fleet") {
        PendingAlliance& state = get_state(guild_id, user_id);

        std::uint64_t guild_id_u64 = static_cast<std::uint64_t>(guild_id);
        unsigned short max_ships = 6;

        try {
            odb::transaction t(db->begin());
            std::unique_ptr<BotSettings> s(db->load<BotSettings>(guild_id_u64));
            max_ships = s->default_max_ships();
            t.commit();
        }
        catch (const odb::object_not_persistent&) {
            dpp::message msg(
                "❌ Ce serveur n'est pas encore configuré.\n"
                "Lance d'abord `/setup` pour définir les salons et rôles."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }
        catch (const std::exception& ex) {
            dpp::message msg(
                std::string("❌ Erreur DB lors du chargement des paramètres : ")
                + ex.what()
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        if (max_ships == 0)
            max_ships = 1;
        if (max_ships > 6)
            max_ships = 6;

        state.max_ships = max_ships;
        state.ships.clear();
        state.ships.resize(max_ships);
        state.current_ship = 0;
        state.fleet_config_started = true;

        send_ship_config_prompt(event, guild_id, user_id, state);
        return true;
    }

    if (id == "create_alliance_ship_next") {
        PendingAlliance& state = get_state(guild_id, user_id);

        if (!state.fleet_config_started || state.ships.empty()) {
            dpp::message msg("❌ Commence par cliquer sur **Configurer la flotte**.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        if (state.current_ship >= state.ships.size())
            state.current_ship = state.ships.size() - 1;

        ShipConfig& sc = state.ships[state.current_ship];

        if (!sc.has_hull || !sc.has_role) {
            dpp::message msg("❌ Merci de choisir **coque** et **rôle** pour ce bateau.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        if (state.current_ship + 1 < state.ships.size()) {
            state.current_ship++;
            send_ship_config_prompt(event, guild_id, user_id, state);
        } else {
            dpp::message msg(
                "✅ Flotte configurée ! Tu peux maintenant terminer avec le dernier écran."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
        }

        return true;
    }

    if (id == "create_alliance_ship_finish") {
        PendingAlliance& state = get_state(guild_id, user_id);

        if (!state.fleet_config_started || state.ships.empty()) {
            dpp::message msg(
                "❌ Tu dois d'abord configurer la flotte avec **Configurer la flotte**."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        if (state.current_ship >= state.ships.size())
            state.current_ship = state.ships.size() - 1;
        {
            ShipConfig& sc = state.ships[state.current_ship];
            if (!sc.has_hull || !sc.has_role) {
                dpp::message msg(
                    "❌ Merci de choisir **coque** et **rôle** pour ce dernier bateau."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }
        }

        if (!state.reprise_set) {
            state.reprise = false;
            state.reprise_set = true;
        }

        if (!state.ready_basic()) {
            std::string missing;
            if (state.date_iso.empty())   missing += "- la date\n";
            if (state.start_time.empty()) missing += "- l'heure de début\n";
            if (state.sale_time.empty())  missing += "- l'heure de vente\n";

            dpp::message msg(
                "❌ Impossible de créer l'alliance, il manque :\n" + missing
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        std::uint64_t guild_id_u64 = static_cast<std::uint64_t>(guild_id);
        std::uint64_t alliance_forum_channel_id = 0;
        std::uint64_t ping_channel_id           = 0;
        std::uint64_t notify_role_id            = 0;
        unsigned short max_ships                = 6;

        try {
            odb::transaction t(db->begin());
            std::unique_ptr<BotSettings> s(db->load<BotSettings>(guild_id_u64));
            alliance_forum_channel_id = s->alliance_forum_channel_id();
            ping_channel_id           = s->ping_channel_id();
            notify_role_id            = s->notify_role_id();
            max_ships                 = s->default_max_ships();
            t.commit();
        }
        catch (const odb::object_not_persistent&) {
            dpp::message msg(
                "❌ Ce serveur n'est pas encore configuré.\n"
                "Lance d'abord `/setup` pour définir les salons et rôles."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }
        catch (const std::exception& ex) {
            dpp::message msg(
                std::string("❌ Erreur DB lors du chargement des paramètres : ")
                + ex.what()
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        if (alliance_forum_channel_id == 0) {
            dpp::message msg(
                "❌ Aucun salon **forum d'alliances** n'est configuré.\n"
                "Va dans `/setup` → **Salons** pour le définir."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        if (max_ships == 0 || max_ships > 6) {
            max_ships = 6;
        }

        std::time_t scheduled_at = 0;
        std::time_t sale_at      = 0;

        if (!make_time_t(state.date_iso, state.start_time, scheduled_at)) {
            dpp::message msg("❌ Impossible d'interpréter l'heure de début.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        if (!make_time_t(state.date_iso, state.sale_time, sale_at)) {
            dpp::message msg("❌ Impossible d'interpréter l'heure de vente.");
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        if (sale_at <= scheduled_at) {
            int year = 0, month = 0, day = 0;
            if (!parse_iso_date(state.date_iso, year, month, day)) {
                dpp::message msg(
                    "❌ Impossible d'interpréter la date de l'alliance."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            std::tm tm_day {};
            tm_day.tm_year = year - 1900;
            tm_day.tm_mon  = month - 1;
            tm_day.tm_mday = day;
            tm_day.tm_hour = 12;
            tm_day.tm_min  = 0;
            tm_day.tm_sec  = 0;

            std::time_t base = std::mktime(&tm_day);
            if (base == -1) {
                dpp::message msg(
                    "❌ Impossible de calculer la date de fin de l'alliance."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            base += 24 * 3600;
            std::tm* tm_next = std::localtime(&base);
            if (!tm_next) {
                dpp::message msg(
                    "❌ Impossible de calculer la date du lendemain."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            char buf[32];
            std::snprintf(
                buf,
                sizeof(buf),
                "%04d-%02d-%02d",
                tm_next->tm_year + 1900,
                tm_next->tm_mon + 1,
                tm_next->tm_mday
            );
            std::string iso_next_day = buf;

            if (!make_time_t(iso_next_day, state.sale_time, sale_at)) {
                dpp::message msg(
                    "❌ Impossible d'interpréter l'heure de vente (lendemain)."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }

            if (sale_at <= scheduled_at) {
                dpp::message msg(
                    "❌ L'heure de vente doit être **après** l'heure de début."
                );
                msg.set_flags(dpp::m_ephemeral);
                event.reply(msg);
                return true;
            }
        }


        std::time_t now = std::time(nullptr);
        if (scheduled_at <= now) {
            dpp::message msg(
                "❌ L'heure de début doit être dans le futur."
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        const std::string alliance_name = random_alliance_name();

        std::string bras_droit_str;
        if (state.bras_droit_id) {
            bras_droit_str = "<@"
                + std::to_string(static_cast<std::uint64_t>(state.bras_droit_id))
                + ">";
        }

        std::string organizer_str =
            "<@" + std::to_string(static_cast<std::uint64_t>(event.command.usr.id)) + ">";

        std::uint64_t alliance_id = 0;
        std::uint64_t organizer_id =
            static_cast<std::uint64_t>(event.command.usr.id);

        try {
            odb::transaction t(db->begin());

            Alliance alliance(
                guild_id_u64,
                organizer_id,
                alliance_name,
                scheduled_at,
                sale_at,
                max_ships
            );
            alliance.right_hand(bras_droit_str);
            alliance.ships_reuse_planned(state.reprise);

            db->persist(alliance);
            alliance_id = alliance.id();

            unsigned short slot = 1;
            for (const auto& sc : state.ships) {
                HullType db_hull = HullType::brig;

                switch (sc.hull) {
                    case ShipHull::Sloop:   db_hull = HullType::sloop;   break;
                    case ShipHull::Brig:    db_hull = HullType::brig;    break;
                    case ShipHull::Galleon: db_hull = HullType::galleon; break;
                }

                std::string role = sc.role.empty() ? "Libre" : sc.role;

                Ship ship(
                    alliance_id,
                    slot,
                    db_hull,
                    role
                );

                db->persist(ship);
                ++slot;
            }

            t.commit();
        }
        catch (const std::exception& ex) {
            dpp::message msg(
                std::string("❌ Erreur DB lors de la création de l'alliance : ")
                + ex.what()
            );
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
            return true;
        }

        std::string start_ts = "<t:" + std::to_string(scheduled_at) + ":t>";
        std::string sale_ts  = "<t:" + std::to_string(sale_at) + ":t>";

        {
            std::ostringstream oss;
            oss << "✅ Alliance **" << alliance_name << "** créée !\n"
                << "Début : " << start_ts << "\n"
                << "Vente : " << sale_ts << "\n"
                << "Bateaux max (paramètre serveur) : " << max_ships << "\n"
                << "Création du post dans le forum d'alliances...";

            dpp::message msg;
            msg.set_content(oss.str());
            msg.set_flags(dpp::m_ephemeral);
            event.reply(msg);
        }

        dpp::cluster* cluster = event.from()->creator;
        if (!cluster) {
            clear_state(guild_id, user_id);
            return true;
        }

        std::tm tm_start {};
#ifdef _WIN32
        localtime_s(&tm_start, &scheduled_at);
#else
        tm_start = *std::localtime(&scheduled_at);
#endif

        std::ostringstream title_oss;
        title_oss << alliance_helpers::french_day_name(tm_start) << " "
                  << std::setw(2) << std::setfill('0') << tm_start.tm_mday
                  << "/"
                  << std::setw(2) << std::setfill('0') << (tm_start.tm_mon + 1)
                  << " " << alliance_helpers::format_hhmm(scheduled_at)
                  << " - " << alliance_helpers::format_hhmm(sale_at);

        std::string thread_title = title_oss.str();

        dpp::message starter_msg;
        starter_msg.set_content("🏴‍☠️ **" + alliance_name + "**.");

        dpp::snowflake forum_channel_sf(alliance_forum_channel_id);

        clear_state(guild_id, user_id);

        cluster->thread_create_in_forum(
            thread_title,
            forum_channel_sf,
            starter_msg,
            dpp::arc_1_day,
            0,
            {},
            [db,
             alliance_id,
             scheduled_at, sale_at,
             ping_channel_id, notify_role_id,
             cluster]
            (const dpp::confirmation_callback_t& cb) {

                if (cb.is_error()) {
                    std::cerr << "[Alliance] Erreur création thread: "
                              << cb.get_error().message << "\n";
                    return;
                }

                dpp::thread thr = cb.get<dpp::thread>();
                dpp::snowflake thread_id = thr.id;

                try {
                    odb::transaction t2(db->begin());

                    std::unique_ptr<Alliance> a(db->load<Alliance>(alliance_id));
                    a->thread_channel_id(
                        static_cast<std::uint64_t>(thread_id)
                    );
                    db->update(*a);

                    AllianceDiscordObject thread_obj(
                        alliance_id,
                        DiscordObjectType::thread,
                        static_cast<std::uint64_t>(thread_id),
                        thr.name,
                        true
                    );
                    db->persist(thread_obj);

                    t2.commit();
                }
                catch (const std::exception& ex) {
                    std::cerr << "[Alliance] Erreur DB maj thread_id / thread_obj : "
                              << ex.what() << "\n";
                }

                alliance_helpers::create_or_update_alliance_roster_message(
                    cluster,
                    db,
                    alliance_id,
                    thread_id
                );

                if (ping_channel_id != 0 && notify_role_id != 0) {
                    std::string start_ts2 =
                        "<t:" + std::to_string(scheduled_at) + ":t>";
                    std::string sale_ts2  =
                        "<t:" + std::to_string(sale_at) + ":t>";

                    std::string content =
                        "<@&" + std::to_string(notify_role_id) + "> "
                        "Nouvelle alliance planifiée !\n"
                        "Début : " + start_ts2 + "\n"
                        "Vente : " + sale_ts2 + "\n"
                        "Thread : <#" + std::to_string(
                            static_cast<std::uint64_t>(thread_id)
                        ) + ">";

                    dpp::message ping_msg(
                        static_cast<dpp::snowflake>(ping_channel_id),
                        content
                    );
                    cluster->message_create(ping_msg);
                }
            }
        );

        return true;
    }

    return false;
}
