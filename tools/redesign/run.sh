#!/bin/bash
# Launch the locally built VLC with the local core and plugins.
# bin/vlc (the libtool wrapper) is required: bin/.libs/vlc links the system libvlc.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export VLC_PLUGIN_PATH="$ROOT/modules/.libs"
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-xcb}
export LC_ALL=C
exec "$ROOT/bin/vlc" --no-plugins-cache --intf qt "$@"
