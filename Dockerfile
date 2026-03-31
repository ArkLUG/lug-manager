FROM ubuntu:22.04 as builder

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    libcurl4-openssl-dev \
    sqlite3 \
    libsqlite3-dev \
    libssl-dev \
    git \
    curl \
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

RUN cmake --preset=release && cmake --build build/release

FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libcurl4 \
    sqlite3 \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /build/build/release/lug_manager /app/lug_manager
COPY --from=builder /build/src/templates /app/src/templates
COPY --from=builder /build/src/static /app/src/static
COPY --from=builder /build/sql /app/sql

ENV TEMPLATES_DIR=/app/src/templates
ENV DATABASE_PATH=/app/lug.db

EXPOSE 8080

CMD ["./lug_manager"]
