#pragma once

#include <memory>
#include <string>

#include <dpp/dpp.h>

namespace odb { namespace pgsql { class database; } }

class IModalUI {
public:
    virtual ~IModalUI() = default;

    virtual bool handle_modal(const dpp::form_submit_t& event,
                              const std::shared_ptr<odb::pgsql::database>& db) const = 0;

protected:

    std::string get_text_field(const dpp::form_submit_t& event,
                               std::size_t row,
                               std::size_t col) const
    {
        if (event.components.size() <= row) return {};
        if (event.components[row].components.size() <= col) return {};
        try {
            return std::get<std::string>(event.components[row].components[col].value);
        } catch (...) {
            return {};
        }
    }

    int get_int_field(const dpp::form_submit_t& event,
                      std::size_t row,
                      std::size_t col,
                      int default_value = 0) const
    {
        std::string s = get_text_field(event, row, col);
        if (s.empty()) return default_value;
        try {
            return std::stoi(s);
        } catch (...) {
            return default_value;
        }
    }

    void reply_ephemeral(const dpp::form_submit_t& event,
                         const std::string& message) const
    {
        dpp::message m(message);
        m.set_flags(dpp::m_ephemeral);
        event.reply(m);
    }
};
