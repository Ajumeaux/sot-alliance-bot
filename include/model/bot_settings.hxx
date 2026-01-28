#pragma once

#include <string>
#include <cstdint>
#include <ctime>

#include <odb/core.hxx>

#pragma db object table("bot_settings")
class BotSettings {
public:
    BotSettings() = default;

    explicit BotSettings(std::uint64_t guild_id)
        : guild_id_(guild_id),
          command_channel_id_(0),
          ping_channel_id_(0),
          alliance_forum_channel_id_(0),
          log_channel_id_(0),
          organizer_role_id_(0),
          notify_role_id_(0),
          default_max_ships_(6),
          allow_public_join_(true),
          timezone_("Europe/Paris"),
          language_("fr"),
          created_at_(std::time(nullptr)),
          updated_at_(std::time(nullptr))
    {}

    std::uint64_t guild_id() const { return guild_id_; }

    std::uint64_t command_channel_id() const { return command_channel_id_; }
    void command_channel_id(std::uint64_t id) { command_channel_id_ = id; touch(); }

    std::uint64_t ping_channel_id() const { return ping_channel_id_; }
    void ping_channel_id(std::uint64_t id) { ping_channel_id_ = id; touch(); }

    std::uint64_t alliance_forum_channel_id() const { return alliance_forum_channel_id_; }
    void alliance_forum_channel_id(std::uint64_t id) { alliance_forum_channel_id_ = id; touch(); }

    std::uint64_t log_channel_id() const { return log_channel_id_; }
    void log_channel_id(std::uint64_t id) { log_channel_id_ = id; touch(); }

    std::uint64_t organizer_role_id() const { return organizer_role_id_; }
    void organizer_role_id(std::uint64_t id) { organizer_role_id_ = id; touch(); }

    std::uint64_t notify_role_id() const { return notify_role_id_; }
    void notify_role_id(std::uint64_t id) { notify_role_id_ = id; touch(); }

    unsigned short default_max_ships() const { return default_max_ships_; }
    void default_max_ships(unsigned short v) { default_max_ships_ = v; touch(); }

    bool allow_public_join() const { return allow_public_join_; }
    void allow_public_join(bool v) { allow_public_join_ = v; touch(); }

    const std::string& timezone() const { return timezone_; }
    void timezone(const std::string& tz) { timezone_ = tz; touch(); }

    const std::string& language() const { return language_; }
    void language(const std::string& lang) { language_ = lang; touch(); }

    std::time_t created_at() const { return created_at_; }
    std::time_t updated_at() const { return updated_at_; }

private:
    friend class odb::access;

    void touch() { updated_at_ = std::time(nullptr); }

    #pragma db id
    std::uint64_t guild_id_;

    std::uint64_t command_channel_id_;
    std::uint64_t ping_channel_id_;
    std::uint64_t alliance_forum_channel_id_;
    std::uint64_t log_channel_id_;

    std::uint64_t organizer_role_id_;
    std::uint64_t notify_role_id_;

    unsigned short default_max_ships_;
    bool           allow_public_join_;

    std::string timezone_;
    std::string language_;

    std::time_t created_at_;
    std::time_t updated_at_;
};
