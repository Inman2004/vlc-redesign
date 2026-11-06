#!/usr/bin/env python3
"""Click inside the VLC main window at window-relative X Y (XTest)."""
import sys, time
from Xlib import display, X
from Xlib.ext import xtest
d = display.Display(); root = d.screen().root
NET = d.intern_atom('_NET_CLIENT_LIST')
for wid in root.get_full_property(NET, X.AnyPropertyType).value:
    w = d.create_resource_object('window', wid)
    try:
        if 'vlc' in [c.lower() for c in (w.get_wm_class() or ())] and 'VLC' in str(w.get_wm_name()):
            break
    except Exception: pass
p = w.translate_coords(root, 0, 0)
x, y = -p.x + int(sys.argv[1]), -p.y + int(sys.argv[2])
btn = int(sys.argv[3]) if len(sys.argv) > 3 else 1
xtest.fake_input(d, X.MotionNotify, x=x, y=y); d.sync(); time.sleep(0.2)
xtest.fake_input(d, X.ButtonPress, btn); d.sync(); time.sleep(0.05)
xtest.fake_input(d, X.ButtonRelease, btn); d.sync()
