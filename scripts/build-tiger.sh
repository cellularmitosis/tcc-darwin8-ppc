#!/bin/sh
# build-tiger.sh — build tcc on Tiger PowerPC.
#
# Wraps the configure + make sequence with the Tiger-specific bits:
#   - --config-semlock=no:  tcc.h's __APPLE__ branch hardcodes
#                            Grand Central Dispatch (10.6+); Tiger
#                            has POSIX semaphores only.
#   - /opt/make-4.3/bin/make: system make is GNU 3.80, lacks $(or).
#                              Auto-installs via tiger.sh if missing.
#
# Run from the repo root or from tcc/.

set -e

cd "$(dirname "$0")/../tcc"

MAKE=/opt/make-4.3/bin/make
if [ ! -x "$MAKE" ]; then
    if command -v tiger.sh >/dev/null 2>&1; then
        echo "make-4.3 not found; installing via tiger.sh..."
        tiger.sh make-4.3
    else
        echo "ERROR: $MAKE not found and tiger.sh is unavailable."
        echo "Install GNU make >= 3.81 manually and re-run with MAKE=..."
        exit 1
    fi
fi

if [ ! -f config.mak ] || [ "$1" = "configure" ]; then
    ./configure --config-semlock=no
    shift 2>/dev/null || :
fi

exec "$MAKE" "$@"
