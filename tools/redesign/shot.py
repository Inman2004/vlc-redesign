#!/usr/bin/env python3
"""Resize the running VLC window and capture it with spectacle.

usage: shot.py OUT.png [WIDTH HEIGHT]
Needs python-xlib (use a venv) and spectacle. Works under XWayland.
"""
import subprocess, sys, time
from Xlib import display, X

out = sys.argv[1]
w = int(sys.argv[2]) if len(sys.argv) > 2 else 1500
h = int(sys.argv[3]) if len(sys.argv) > 3 else 844

d = display.Display()
root = d.screen().root
NET_CLIENTS = d.intern_atom('_NET_CLIENT_LIST')

# VLC_PID=<pid> targets one instance when several are open
import os
PID = int(os.environ.get('VLC_PID', '0'))
NET_WM_PID = d.intern_atom('_NET_WM_PID')

def pid_matches(win):
    p = win.get_full_property(NET_WM_PID, X.AnyPropertyType)
    return bool(p) and p.value[0] == PID

def find():
    ids = root.get_full_property(NET_CLIENTS, X.AnyPropertyType)
    for wid in (ids.value if ids else []):
        win = d.create_resource_object('window', wid)
        try:
            cls = win.get_wm_class() or ()
            name = win.get_wm_name() or ''
        except Exception:
            continue
        if 'vlc' not in [c.lower() for c in cls]:
            continue
        # with VLC_PID: that process's window, whatever it is titled
        # (the playing file's name while something plays)
        if PID:
            if pid_matches(win):
                return win
        elif 'VLC' in str(name):
            return win
    return None

win = None
for _ in range(40):
    win = find()
    if win: break
    time.sleep(0.25)
if not win:
    sys.exit('VLC window not found')

win.configure(x=0, y=0, width=w, height=h, stack_mode=X.Above)
d.sync()
time.sleep(1.5)
geo = win.get_geometry()
pos = win.translate_coords(root, 0, 0)
x, y = -pos.x, -pos.y
full = out + '.full.png'
subprocess.run(['spectacle', '-b', '-n', '-f', '-o', full], check=True)
time.sleep(0.5)
subprocess.run(['magick', full, '-crop', f'{geo.width}x{geo.height}+{x}+{y}', '+repage', out], check=True)
print(out, geo.width, geo.height, x, y)
