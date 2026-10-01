#!/bin/sh
# Runs lug_manager as the unprivileged "lug" user.
#
# Started as root only long enough to hand /app/data to that user: volumes
# created by older images (which ran as root) are root-owned, and the app
# must be able to write the SQLite DB and uploads there. After that,
# privileges are dropped for good via setpriv before exec'ing the server.
#
# PUID / PGID (optional, e.g. 99 / 100 on Unraid) make "lug" use those ids,
# so files in the data folder belong to the host user you expect.
set -eu

if [ "$(id -u)" = "0" ]; then
    if [ -n "${PGID:-}" ] && [ "$PGID" != "$(id -g lug)" ]; then
        groupmod -o -g "$PGID" lug
    fi
    if [ -n "${PUID:-}" ] && [ "$PUID" != "$(id -u lug)" ]; then
        usermod -o -u "$PUID" lug
    fi
    mkdir -p /app/data
    chown -R lug:lug /app/data
    exec setpriv --reuid=lug --regid=lug --init-groups "$@"
fi

exec "$@"
