#pragma once

#include <string>
#include <cstdint>
#include <ctime>

#include <odb/core.hxx>

enum class DiscordObjectType {
    role          = 0,
    voice_channel = 1,
    text_channel  = 2,
    category      = 3,
    thread        = 4,
    message       = 5
};

#pragma db value(DiscordObjectType) type("smallint")

#pragma db object table("alliance_discord_objects")
class AllianceDiscordObject {
public:
    AllianceDiscordObject() = default;

    AllianceDiscordObject(std::uint64_t alliance_id,
                          DiscordObjectType type,
                          std::uint64_t discord_id,
                          std::string name,
                          bool auto_delete = true)
        : alliance_id_(alliance_id),
          type_(type),
          discord_id_(discord_id),
          name_(std::move(name)),
          auto_delete_(auto_delete),
          created_at_(std::time(nullptr)),
          deleted_at_(0)
    {}

    std::uint64_t id() const { return id_; }

    std::uint64_t alliance_id() const { return alliance_id_; }

    DiscordObjectType type() const { return type_; }
    void type(DiscordObjectType t) { type_ = t; }

    std::uint64_t discord_id() const { return discord_id_; }
    void discord_id(std::uint64_t id) { discord_id_ = id; }

    const std::string& name() const { return name_; }
    void name(const std::string& n) { name_ = n; }

    bool auto_delete() const { return auto_delete_; }
    void auto_delete(bool v) { auto_delete_ = v; }

    std::time_t created_at() const { return created_at_; }

    std::time_t deleted_at() const { return deleted_at_; }
    void mark_deleted_now() { deleted_at_ = std::time(nullptr); }

private:
    friend class odb::access;

    #pragma db id auto
    std::uint64_t id_;

    std::uint64_t alliance_id_;

    DiscordObjectType type_;
    std::uint64_t     discord_id_;
    std::string       name_;

    bool        auto_delete_;
    std::time_t created_at_;
    std::time_t deleted_at_;
};
