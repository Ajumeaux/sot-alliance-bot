# === STAGE 1: build ===
FROM ubuntu:22.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

# Build tools + ODB + Postgres headers
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    libpq-dev \
    libodb-dev \
    libodb-pgsql-dev \
    odb \
    gcc-10 g++-10 \
    && rm -rf /var/lib/apt/lists/*

# Install DPP (library + headers)
RUN wget -O /tmp/dpp.deb https://dl.dpp.dev/ \
 && apt-get update \
 && apt-get install -y /tmp/dpp.deb \
 && rm -rf /var/lib/apt/lists/* /tmp/dpp.deb

WORKDIR /app

# Copy the whole project
COPY . .

# === Generate ODB files into /app/generated ===
RUN mkdir -p generated && \
    odb -d pgsql \
        --std c++11 \
        --generate-query \
        --generate-schema \
        --schema-format embedded \
        --output-dir generated \
        -Iinclude \
        include/model/users.hxx \
        include/model/alliances.hxx \
        include/model/ships.hxx \
        include/model/alliance_participants.hxx \
        include/model/bot_settings.hxx \
        include/model/alliance_discord_objects.hxx

# === CMake build ===
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j"$(nproc)"

# === STAGE 2: runtime ===
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libpq5 \
    libodb-dev \
    libodb-pgsql-dev \
    wget \
    ca-certificates \
    tzdata \
    && rm -rf /var/lib/apt/lists/*

# DPP runtime
RUN wget -O /tmp/dpp.deb https://dl.dpp.dev/ \
 && apt-get update \
 && apt-get install -y /tmp/dpp.deb \
 && rm -rf /var/lib/apt/lists/* /tmp/dpp.deb

WORKDIR /app

# Only copy the compiled binary
COPY --from=build /app/build/discord-bot /usr/local/bin/discord-bot

ENTRYPOINT ["/usr/local/bin/discord-bot"]
