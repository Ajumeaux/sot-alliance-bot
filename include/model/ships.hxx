#pragma once

#include <string>
#include <cstdint>
#include <ctime>

#include <odb/core.hxx>

enum class HullType {
    sloop   = 0,
    brig    = 1,
    galleon = 2
};

#pragma db value(HullType) type("smallint")

#pragma db object table("ships")
class Ship {
public:
    Ship() = default;

    Ship(std::uint64_t alliance_id,
         unsigned short slot,
         HullType hull_type,
         std::string crew_role)
        : alliance_id_(alliance_id),
          slot_(slot),
          hull_type_(hull_type),
          crew_role_(std::move(crew_role)),
          created_at_(std::time(nullptr))
    {}

    std::uint64_t id() const { return id_; }

    std::uint64_t alliance_id() const { return alliance_id_; }

    unsigned short slot() const { return slot_; }
    void slot(unsigned short s) { slot_ = s; }

    HullType hull_type() const { return hull_type_; }
    void hull_type(HullType h) { hull_type_ = h; }

    const std::string& crew_role() const { return crew_role_; }
    void crew_role(const std::string& r) { crew_role_ = r; }

    std::time_t created_at() const { return created_at_; }

private:
    friend class odb::access;

    #pragma db id auto
    std::uint64_t id_;

    std::uint64_t alliance_id_;
    unsigned short slot_;

    HullType     hull_type_;
    std::string  crew_role_;

    std::time_t created_at_;
};
