FROM ubuntu:24.04 AS builder

# tzdata: IANA time zones (America/Chicago ...) for Discord/calendar times;
# without it every zone silently falls back to UTC.
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    tzdata \
    cmake \
    g++ \
    make \
    pkg-config \
    libcurl4-openssl-dev \
    sqlite3 \
    libsqlite3-dev \
    libssl-dev \
    zlib1g-dev \
    git \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY . .

# Generate production Tailwind CSS from templates
RUN mkdir -p src/static && \
    curl -sL https://github.com/tailwindlabs/tailwindcss/releases/latest/download/tailwindcss-linux-x64 \
      -o /usr/local/bin/tailwindcss && \
    chmod +x /usr/local/bin/tailwindcss && \
    tailwindcss -i src/css/input.css \
      --content "src/templates/**/*.html" \
      -o src/static/tailwind.min.css \
      --minify

# The version shown on the About page (CI passes the tag or commit)
ARG LUG_VERSION=dev
RUN cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DLUG_VERSION="${LUG_VERSION}" && cmake --build build -j"$(nproc)"

# Run tests inside the build (skip integration tests that need a running server)
RUN ctest --test-dir build --output-on-failure

# --- Runtime stage ---
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get upgrade -y && \
    apt-get install -y --no-install-recommends \
    libcurl4t64 \
    tzdata \
    sqlite3 \
    libssl3t64 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Unprivileged runtime user - see docker-entrypoint.sh
RUN groupadd --system lug && useradd --system --gid lug --home-dir /app --shell /usr/sbin/nologin lug

WORKDIR /app

COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
COPY --from=builder /build/build/lug_manager /app/lug_manager
COPY --from=builder /build/src/templates     /app/src/templates
COPY --from=builder /build/src/static        /app/src/static
COPY --from=builder /build/sql               /app/sql

ENV LUG_TEMPLATES_DIR=/app/src/templates
ENV LUG_DB_PATH=/app/data/lug.db
ENV LUG_PORT=8080

VOLUME /app/data

EXPOSE 8080

# /healthz answers "ok" when the server and its database work (Unraid and
# docker ps show the container as healthy / unhealthy).
HEALTHCHECK --interval=60s --timeout=5s --start-period=60s --retries=3 CMD ["/app/lug_manager", "--healthcheck"]

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["./lug_manager"]
