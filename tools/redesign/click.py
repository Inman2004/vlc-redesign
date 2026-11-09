#!/usr/bin/env python3
"""Click inside the VLC main window at window-relative X Y (XTest)."""
import sys, time
from Xlib import display, X
from Xlib.ext import xtest
d = display.Display(); root = d.screen().root
NET = d.intern_atom('_NET_CLIENT_LIST')
import os
PID = int(os.environ.get('VLC_PID', '0'))  # target one instance when several are open
NET_WM_PID = d.intern_atom('_NET_WM_PID')
# Only ever click a VLC window: with VLC_PID set, the one of that process
# (its title is the playing file, not "VLC ..."); never fall through to
# whatever window happens to be last in the list.
target = None
for wid in root.get_full_property(NET, X.AnyPropertyType).value:
    w = d.create_resource_object('window', wid)
    try:
        if 'vlc' not in [c.lower() for c in (w.get_wm_class() or ())]:
            continue
        if PID:
            p = w.get_full_property(NET_WM_PID, X.AnyPropertyType)
            if p and p.value[0] == PID:
                target = w
                break
        elif 'VLC' in str(w.get_wm_name()):
            target = w
            break
    except Exception: pass
if target is None:
    sys.exit('VLC window not found')
w = target
p = w.translate_coords(root, 0, 0)
x, y = -p.x + int(sys.argv[1]), -p.y + int(sys.argv[2])
btn = int(sys.argv[3]) if len(sys.argv) > 3 else 1
xtest.fake_input(d, X.MotionNotify, x=x, y=y); d.sync(); time.sleep(0.2)
xtest.fake_input(d, X.ButtonPress, btn); d.sync(); time.sleep(0.05)
xtest.fake_input(d, X.ButtonRelease, btn); d.sync()
