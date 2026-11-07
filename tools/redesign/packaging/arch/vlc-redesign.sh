#!/bin/sh
# The embedded video window needs X11; under Wayland Qt runs through XWayland.
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
exec /opt/vlc-redesign/bin/vlc "$@"
