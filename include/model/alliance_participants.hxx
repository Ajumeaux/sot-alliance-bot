#pragma once

#include <cstdint>
#include <ctime>

#include <odb/core.hxx>

#pragma db object table("alliance_participants")
class AllianceParticipant {
public:
    AllianceParticipant() = default;

    AllianceParticipant(std::uint64_t alliance_id,
                        std::uint64_t user_id,
                        std::uint64_t ship_id = 0)
        : alliance_id_(alliance_id),
          user_id_(user_id),
          ship_id_(ship_id),
          joined_at_(std::time(nullptr)),
          left_at_(0) // 0 = pas encore parti
    {}

    std::uint64_t id() const { return id_; }

    std::uint64_t alliance_id() const { return alliance_id_; }
    std::uint64_t user_id() const { return user_id_; }

    std::uint64_t ship_id() const { return ship_id_; }
    void ship_id(std::uint64_t s) { ship_id_ = s; }

    std::time_t joined_at() const { return joined_at_; }
    std::time_t left_at() const { return left_at_; }

    void left_now() { left_at_ = std::time(nullptr); }

private:
    friend class odb::access;

    #pragma db id auto
    std::uint64_t id_;

    std::uint64_t alliance_id_;
    std::uint64_t user_id_;

    std::uint64_t ship_id_;

    std::time_t joined_at_;
    std::time_t left_at_;
};
