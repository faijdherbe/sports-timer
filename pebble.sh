#!/bin/sh
# Runs the Pebble SDK in Docker on this project.
#   ./pebble.sh build                      -> build/app.pbw
#   ./pebble.sh emu [aplite|diorite|flint]  -> emulator window (X11) + app logs
#   ./pebble.sh login                      -> GitHub login for Dev Connect (once)
#   ./pebble.sh install --cloudpebble      -> install on the watch via the phone app
#   ./pebble.sh <any pebble command>
set -e
docker image inspect pebble-sdk >/dev/null 2>&1 || docker build -t pebble-sdk "$(dirname "$0")"
case "$1" in
  # Always clean: waf does not notice new messageKeys in package.json. Full build is < 1s.
  build) set -- sh -c "pebble clean >/dev/null && pebble build" ;;
  emu) set -- sh -c "pebble clean >/dev/null && pebble build && pebble install --emulator ${2:-diorite} && pebble logs --emulator ${2:-diorite}" ;;
  *) set -- pebble "$@" ;;
esac
[ -t 0 ] && TTY=-it
AUTH="${XDG_CONFIG_HOME:-$HOME/.config}/pebble-docker"
mkdir -p "$AUTH"
exec docker run --rm $TTY --network host -u "$(id -u):$(id -g)" -v "$PWD":/app \
  -v "$AUTH":/pebble/auth -e DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix \
  ${XAUTHORITY:+-e XAUTHORITY -v "$XAUTHORITY:$XAUTHORITY:ro"} \
  pebble-sdk "$@"
