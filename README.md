# SoT Alliance Bot (DPP + C++)

Discord bot written in **C++** using **[DPP](https://dpp.dev/)**, designed to organize **Sea of Thieves “Alliance Server” events** on a Discord guild (FR-focused).
It helps schedule an alliance, let members join a specific ship/crew, and (optionally) creates temporary voice channels and roles during the event.

Persistence is handled with **PostgreSQL** + **ODB**.

> Note: The bot is used on a French-speaking community, so the slash subcommands are currently **hardcoded in French**.

---

## What is an “alliance server” event?

An “alliance server” event is a coordinated activity where many players launch Sea of Thieves at the same time to maximize the chances of landing on the same in-game server,
then play together all day in a friendly “PVE-focused” environment (multiple crews, often 20+ players).

---

## Features

- `/alliance creer` : create a new alliance event (date/time + fleet/ships configuration)
- `/alliance rejoindre` : join an alliance (also used to switch ship)
- `/alliance quitter` : leave an alliance
- `/alliance modifier` : edit an existing alliance (date/time, ships, etc.)
- `/alliance annuler` : cancel a scheduled alliance
- `/alliance demarrer` : start the alliance (create temporary voice channels + assign roles)
- `/alliance terminer` : end the alliance (cleanup: remove channels + roles)

Configuration (server-specific) via:
- `/alliance setup` (channels, roles, and advanced options like default ship count and timezone)

---

## Usage workflow (typical)

1. An organizer runs `/alliance creer` in the dedicated commands channel
2. The bot creates a forum post for the alliance (all other commands are used inside the bot-created forum thread)
3. Members join via `/alliance rejoindre` and pick a ship/crew (select menus)
4. At event time, the organizer runs `/alliance demarrer`
   - the bot creates temporary voice channels
   - assigns roles to participants for the duration of the alliance
5. When done, `/alliance terminer` cleans everything up

---

## Requirements

- Docker + Docker Compose (recommended)
- Or local build:
  - C++ compiler (C++17)
  - DPP
  - PostgreSQL client libs
  - ODB (and ODB PGSQL runtime)

---

## Run with Docker Compose

### Environment

Create a `.env` file next to `docker-compose.yml`:

```bash
DISCORD_TOKEN=YOUR_DISCORD_BOT_TOKEN_HERE
```

Then start:

```bash
docker compose up -d --build
```

Logs:

```bash
docker compose logs -f
```

Stop:

```bash
docker compose down
```

### docker-compose services

- `db`: Postgres 16
- `bot`: the Discord bot (depends on DB healthcheck)

The bot uses these environment variables:

- `DISCORD_TOKEN` (required)
- `DB_HOST` (default in compose: `db`)
- `DB_PORT` (default: `5432`)
- `DB_USER` (default: `botuser`)
- `DB_PASSWORD` (default: `botpassword`)
- `DB_NAME` (default: `botdb`)
- `TZ` (default: `Europe/Paris`)

Database init scripts are mounted from:
- `./sql:/docker-entrypoint-initdb.d:ro`

---

## Permissions & Discord notes

- `/alliance setup` is restricted to guild admins (owner or users with `Administrator` permission).
- Commands are intended to be used **inside a server** (not DMs).
- The bot registers a single global slash command: `/alliance` with multiple subcommands.

---

## How it works (architecture)

### Entry point

At startup, the bot:
- reads `DISCORD_TOKEN`
- loads DB config from env
- initializes schema and checks DB connection
- starts the DPP bot loop

### Slash commands design

- One root command: `/alliance`
- Multiple subcommands (hardcoded in French): `setup`, `creer`, `annuler`, `rejoindre`, `quitter`, `demarrer`, `terminer`, `modifier`
- Each subcommand is an `ISlashCommand` implementation:
  - `build_subcommand(...)` declares the subcommand
  - `handle(...)` executes it

### UI interactions

The bot uses Discord components:
- buttons (`on_button_click`)
- select menus (`on_select_click`)
- modals (`on_form_submit`)

Each UI feature has a dedicated handler class (e.g. `SetupUI`, `CreateAllianceUI`, `EditAllianceUI`, etc.).

### Persistence (PostgreSQL + ODB)

Server configuration (channels/roles/options) is stored per guild (example: `BotSettings`):
- command channel
- ping/announce channel
- forum channel where alliance threads are created
- log channel
- organizer role
- notify role
- default max ships
- timezone

Alliance state and participants are stored in Postgres via ODB models under `include/model`.

---

## Project structure

```text
.
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
├── cmake/FindDPP.cmake
├── include/
│   ├── bot/                 # bot core, commands, UI handlers
│   ├── db/                  # database + schema init/checks
│   ├── model/               # ODB models (*.hxx)
│   └── util/                # env helpers
├── generated/               # generated ODB code (if committed)
├── sql/                     # DB init scripts
└── src/
    ├── main.cpp
    ├── bot/                 # implementations
    └── db/
```

---

## Contributing

PRs are welcome (bug fixes, refactors, documentation, improvements).

Notes:
- This bot is currently FR-focused (subcommands are French). Contributions to make the bot i18n-friendly are welcome.

---

## License

Choose a license (MIT is a good default). If you want “do whatever you want”, MIT is recommended.
