#pragma once

#include <string>
#include <cstdint>
#include <ctime>

#include <odb/core.hxx>

#pragma db object table("users")
class User {
public:
    User() = default;

    User(std::uint64_t discord_id, std::string name)
        : discord_id_(discord_id),
          name_(std::move(name)),
          is_banned_(false),
          created_at_(std::time(nullptr)),
          last_alliance_at_(0)
    {}

    std::uint64_t discord_id() const { return discord_id_; }

    const std::string& name() const { return name_; }
    void name(const std::string& n) { name_ = n; }

    const std::string& gamertag() const { return gamertag_; }
    void gamertag(const std::string& g) { gamertag_ = g; }

    bool is_banned() const { return is_banned_; }
    void is_banned(bool b) { is_banned_ = b; }

    const std::string& ban_reason() const { return ban_reason_; }
    void ban_reason(const std::string& r) { ban_reason_ = r; }

    std::time_t created_at() const { return created_at_; }

    std::time_t last_alliance_at() const { return last_alliance_at_; }
    void last_alliance_now() { last_alliance_at_ = std::time(nullptr); }
    void last_alliance_at(std::time_t t) { last_alliance_at_ = t; }

private:
    friend class odb::access;

    #pragma db id
    std::uint64_t discord_id_;

    std::string name_;
    std::string gamertag_;

    bool        is_banned_;
    std::string ban_reason_;

    std::time_t created_at_;
    // 0 = jamais participé à une alliance
    std::time_t last_alliance_at_;
};
