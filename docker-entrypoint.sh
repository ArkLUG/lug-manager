#!/bin/sh
# Runs lug_manager as the unprivileged "lug" user.
#
# Started as root only long enough to hand /app/data to that user: volumes
# created by older images (which ran as root) are root-owned, and the app
# must be able to write the SQLite DB and uploaded logo there. After that,
# privileges are dropped for good via setpriv before exec'ing the server.
set -eu

if [ "$(id -u)" = "0" ]; then
    mkdir -p /app/data
    chown -R lug:lug /app/data
    exec setpriv --reuid=lug --regid=lug --init-groups "$@"
fi

exec "$@"
