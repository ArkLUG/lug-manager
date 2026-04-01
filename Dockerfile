FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    g++ \
    make \
    pkg-config \
    libcurl4-openssl-dev \
    sqlite3 \
    libsqlite3-dev \
    libssl-dev \
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

RUN cmake -B build -S . -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)

# --- Runtime stage ---
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4t64 \
    sqlite3 \
    libssl3t64 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /build/build/lug_manager /app/lug_manager
COPY --from=builder /build/src/templates     /app/src/templates
COPY --from=builder /build/src/static        /app/src/static
COPY --from=builder /build/sql               /app/sql

ENV LUG_TEMPLATES_DIR=/app/src/templates
ENV LUG_DB_PATH=/app/data/lug.db
ENV LUG_PORT=8080

VOLUME /app/data

EXPOSE 8080

CMD ["./lug_manager"]
