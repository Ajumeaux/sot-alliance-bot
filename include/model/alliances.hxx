#pragma once

#include <string>
#include <cstdint>
#include <ctime>

#include <odb/core.hxx>

enum class AllianceStatus {
    planned   = 0,
    matching  = 1,
    in_game   = 2,
    finished  = 3,
    cancelled = 4
};

#pragma db value(AllianceStatus) type("smallint")

#pragma db object table("alliances")
class Alliance {
public:
    Alliance() = default;

    Alliance(std::uint64_t guild_id,
             std::uint64_t organizer_id,
             std::string name,
             std::time_t scheduled_at,
             std::time_t sale_at,
             unsigned short max_ships = 6)
        : guild_id_(guild_id),
          organizer_id_(organizer_id),
          name_(std::move(name)),
          scheduled_at_(scheduled_at),
          sale_at_(sale_at),
          status_(AllianceStatus::planned),
          max_ships_(max_ships),
          right_hand_(),
          ships_reuse_planned_(false),
          thread_channel_id_(0),
          created_at_(std::time(nullptr)),
          updated_at_(std::time(nullptr))
    {}

    std::uint64_t id() const { return id_; }

    std::uint64_t guild_id() const { return guild_id_; }
    std::uint64_t organizer_id() const { return organizer_id_; }

    const std::string& name() const { return name_; }
    void name(const std::string& n) { name_ = n; touch(); }

    std::time_t scheduled_at() const { return scheduled_at_; }
    void scheduled_at(std::time_t t) { scheduled_at_ = t; touch(); }

    std::time_t sale_at() const { return sale_at_; }
    void sale_at(std::time_t t) { sale_at_ = t; touch(); }

    AllianceStatus status() const { return status_; }
    void status(AllianceStatus s) { status_ = s; touch(); }

    unsigned short max_ships() const { return max_ships_; }
    void max_ships(unsigned short m) { max_ships_ = m; touch(); }

    const std::string& right_hand() const { return right_hand_; }
    void right_hand(const std::string& r) { right_hand_ = r; touch(); }

    bool ships_reuse_planned() const { return ships_reuse_planned_; }
    void ships_reuse_planned(bool b) { ships_reuse_planned_ = b; touch(); }

    std::uint64_t thread_channel_id() const { return thread_channel_id_; }
    void thread_channel_id(std::uint64_t id) { thread_channel_id_ = id; touch(); }

    std::time_t created_at() const { return created_at_; }
    std::time_t updated_at() const { return updated_at_; }

    void touch() { updated_at_ = std::time(nullptr); }

private:
    friend class odb::access;

    #pragma db id auto
    std::uint64_t id_;

    std::uint64_t guild_id_;
    std::uint64_t organizer_id_;

    std::string    name_;
    std::time_t    scheduled_at_;
    std::time_t    sale_at_;
    AllianceStatus status_;
    unsigned short max_ships_;

    std::string right_hand_;
    bool        ships_reuse_planned_;

    std::uint64_t thread_channel_id_;

    std::time_t created_at_;
    std::time_t updated_at_;
};
