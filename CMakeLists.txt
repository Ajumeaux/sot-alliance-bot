cmake_minimum_required(VERSION 3.20)

project(discord-bot
    VERSION 1.0
    DESCRIPTION "Discord bot using DPP + ODB + Postgres"
    LANGUAGES CXX
)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Where to look for find_*.cmake modules (FindDPP.cmake)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# === DPP ===
find_package(DPP REQUIRED)

# === ODB / database ===

set(ODB_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")

set(ODB_SOURCES
    "${ODB_GENERATED_DIR}/users-odb.cxx"
    "${ODB_GENERATED_DIR}/alliances-odb.cxx"
    "${ODB_GENERATED_DIR}/ships-odb.cxx"
    "${ODB_GENERATED_DIR}/alliance_participants-odb.cxx"
    "${ODB_GENERATED_DIR}/bot_settings-odb.cxx"
    "${ODB_GENERATED_DIR}/alliance_discord_objects-odb.cxx"
)

add_library(db STATIC ${ODB_SOURCES})

target_link_libraries(db
    PUBLIC
        odb
        odb-pgsql
        pq
)

# Include dirs exported by the db library (also visible to the executable)
target_include_directories(db
    PUBLIC
        "${ODB_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/model"
)

# === Bot executable ===

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/db/Database.cpp
    src/db/Schema.cpp
    src/bot/AllianceBot.cpp
    src/bot/AllianceHelpers.cpp
    src/bot/commands/SetupCommand.cpp
    src/bot/commands/CreateAllianceCommand.cpp
    src/bot/commands/CancelAllianceCommand.cpp
    src/bot/commands/JoinAllianceCommand.cpp
    src/bot/commands/LeaveAllianceCommand.cpp
    src/bot/commands/StartAllianceCommand.cpp
    src/bot/commands/EndAllianceCommand.cpp
    src/bot/commands/EditAllianceCommand.cpp
    src/bot/ui/SetupUI.cpp
    src/bot/ui/CreateAllianceUI.cpp
    src/bot/ui/CancelAllianceUI.cpp
    src/bot/ui/JoinAllianceUI.cpp
    src/bot/ui/LeaveAllianceUI.cpp
    src/bot/ui/EditAllianceUI.cpp
    src/bot/ui/EndAllianceUI.cpp
)

# Executable-specific include dirs (in addition to those inherited via db)
target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/model"
        "${ODB_GENERATED_DIR}"
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${DPP_LIBRARIES}          # or DPP::DPP depending on your FindDPP
        "-Wl,--whole-archive"
        db
        "-Wl,--no-whole-archive"
)
