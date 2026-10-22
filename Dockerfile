# ── Build stage ─────────────────────────────────────────────────────
FROM ubuntu:26.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
        build-essential cmake git \
        libsqlite3-dev libfreetype-dev libncurses-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -S . -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j"$(nproc)" && \
    strip build/lfscan

# ── Runtime stage ───────────────────────────────────────────────────
FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
        libnotify-bin \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd -r lfscan && \
    useradd -r -g lfscan -d /home/lfscan -s /sbin/nologin lfscan

COPY --from=builder /src/build/lfscan /usr/local/bin/lfscan

RUN mkdir -p /home/lfscan/.local/share/lfscan && \
    chown -R lfscan:lfscan /home/lfscan

USER lfscan
WORKDIR /home/lfscan

ENTRYPOINT ["lfscan"]
