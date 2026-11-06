#!/usr/bin/env python3
"""Generate the "modern" icon overlay for the redesigned Qt interface.

Every legacy icon of modules/gui/qt/vlc.qrc that has a modern equivalent is
re-created at the SAME resource path (":/menu/info.svg", ...) from either
  - the Figma-matched glyphs in pixmaps/redesign (R:name), or
  - Iconoir (MIT, https://iconoir.com) (I:name),
tinted to the redesign palette. modern.qrc is registered before vlc.qrc when
qt-redesign is on, and the first registered resource wins, so dialogs,
menus and preferences pick the new icons up without code changes.

usage: gen_modern_icons.py ICONOIR_PACKAGE_DIR
"""
import os, re, shutil, subprocess, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'modules', 'gui', 'qt')
OUT = os.path.join(ROOT, 'pixmaps', 'modern')

GREY = '#8F8F8F'      # icon token
ACCENT = '#D37918'    # default accent
DIM = '#4A4A4A'       # disabled/off state

# legacy resource path -> (source, colour)
MAP = {
    # menus
    '/menu/help.svg':            ('I:help-circle', GREY),
    '/menu/info.svg':            ('I:info-circle', GREY),
    '/menu/messages.svg':        ('I:chat-lines', GREY),
    '/menu/preferences.svg':     ('I:settings', GREY),
    '/menu/settings.svg':        ('I:tools', GREY),
    '/menu/stream.svg':          ('I:antenna-signal', GREY),
    '/menu/playlist_menu.svg':   ('I:playlist', GREY),
    '/menu/exit.svg':            ('R:quit', GREY),
    # toolbar (classic bar, fullscreen bar, toolbar editor, menus)
    '/toolbar/faster.svg':       ('I:fast-arrow-right', GREY),
    '/toolbar/slower.svg':       ('I:fast-arrow-left', GREY),
    '/toolbar/faster2.svg':      ('I:fast-arrow-right', GREY),
    '/toolbar/slower2.svg':      ('I:fast-arrow-left', GREY),
    '/toolbar/skip_fw.svg':      ('I:forward', GREY),
    '/toolbar/skip_back.svg':    ('I:rewind', GREY),
    '/toolbar/play_b.svg':       ('R:play', GREY),
    '/toolbar/pause_b.svg':      ('R:pause', GREY),
    '/toolbar/stop_b.svg':       ('R:stop', GREY),
    '/toolbar/next_b.svg':       ('R:next', GREY),
    '/toolbar/previous_b.svg':   ('R:prev', GREY),
    '/toolbar/eject.svg':        ('I:eject', GREY),
    '/toolbar/fullscreen.svg':   ('R:fullscreen', GREY),
    '/toolbar/defullscreen.svg': ('I:collapse', GREY),
    '/toolbar/renderer.svg':     ('I:chromecast', GREY),
    '/toolbar/extended.svg':     ('R:settings', GREY),
    '/toolbar/playlist.svg':     ('R:queue', GREY),
    '/toolbar/snapshot.svg':     ('I:screenshot', GREY),
    '/toolbar/record.svg':       ('R:record', GREY),
    '/toolbar/atob_nob.svg':     ('I:keyframes-couple', GREY),
    '/toolbar/atob_noa.svg':     ('I:keyframe-plus', ACCENT),
    '/toolbar/atob.svg':         ('I:keyframes-couple', ACCENT),
    '/toolbar/frame.svg':        ('I:frame-simple', GREY),
    '/toolbar/reverse.svg':      ('I:undo', GREY),
    '/toolbar/clear.svg':        ('I:bin', GREY),
    '/toolbar/dvd_menu.svg':     ('I:menu', GREY),
    '/toolbar/dvd_next.svg':     ('I:skip-next', GREY),
    '/toolbar/dvd_prev.svg':     ('I:skip-prev', GREY),
    '/toolbar/volume-high.svg':  ('I:sound-high', GREY),
    '/toolbar/volume-medium.svg':('I:sound-low', GREY),
    '/toolbar/volume-low.svg':   ('I:sound-min', GREY),
    '/toolbar/volume-muted.svg': ('I:sound-off', GREY),
    '/toolbar/tv.svg':           ('I:tv', GREY),
    '/toolbar/tvtelx.svg':       ('I:text-box', GREY),
    # playlist buttons: off/on states must stay distinguishable
    '/buttons/playlist/playlist_add.svg':    ('I:plus', GREY),
    '/buttons/playlist/playlist_remove.svg': ('I:minus', GREY),
    '/buttons/playlist/repeat_off.svg':      ('I:repeat', DIM),
    '/buttons/playlist/repeat_all.svg':      ('I:repeat', ACCENT),
    '/buttons/playlist/repeat_one.svg':      ('I:repeat-once', ACCENT),
    '/buttons/playlist/shuffle_on.svg':      ('I:shuffle', GREY),
    # item types
    '/type/capture-card.svg':    ('I:video-camera', GREY),
    '/type/disc.svg':            ('I:compact-disc', GREY),
    '/type/file-asym.svg':       ('I:page', GREY),
    '/type/file-wide.svg':       ('I:page', GREY),
    '/type/file.svg':            ('I:page', GREY),
    '/type/folder-grey.svg':     ('I:folder', GREY),
    '/type/directory.svg':       ('I:folder', GREY),
    '/type/node.svg':            ('I:folder', GREY),
    '/type/network.svg':         ('I:network', GREY),
    '/type/stream.svg':          ('I:antenna-signal', GREY),
    '/type/playlist.svg':        ('I:playlist', GREY),
    # misc
    '/update.svg':               ('I:refresh-double', GREY),
    '/go-next.svg':              ('I:arrow-right', GREY),
    '/new.svg':                  ('I:plus', GREY),
    '/lock.svg':                 ('I:lock', GREY),
    '/search_clear.svg':         ('I:xmark-circle', GREY),
    '/dropzone.svg':             ('I:import', GREY),
    '/valid.svg':                ('I:check-circle', ACCENT),
    # preferences
    '/prefsmenu/cone_interface_64.png': ('I:app-window', GREY),
    '/prefsmenu/cone_audio_64.png':     ('I:sound-high', GREY),
    '/prefsmenu/cone_video_64.png':     ('I:media-video', GREY),
    '/prefsmenu/cone_subtitles_64.png': ('I:closed-captions-tag', GREY),
    '/prefsmenu/cone_input_64.png':     ('I:cpu', GREY),
    '/prefsmenu/cone_hotkeys_64.png':   ('I:key-command', GREY),
    '/prefsmenu/advanced/audio.svg':    ('I:sound-high', GREY),
    '/prefsmenu/advanced/codec.svg':    ('I:cpu', GREY),
    '/prefsmenu/advanced/extended.svg': ('I:puzzle', GREY),
    '/prefsmenu/advanced/intf.svg':     ('I:app-window', GREY),
    '/prefsmenu/advanced/playlist.svg': ('I:playlist', GREY),
    '/prefsmenu/advanced/sout.svg':     ('I:antenna-signal', GREY),
    '/prefsmenu/advanced/video.svg':    ('I:media-video', GREY),
    # Windows taskbar thumbnail buttons
    '/win7/play.svg':            ('R:play', GREY),
    '/win7/pause.svg':           ('R:pause', GREY),
    '/win7/prev.svg':            ('R:prev', GREY),
    '/win7/next.svg':            ('R:next', GREY),
    # sidebar / services
    '/sidebar/disc.svg':         ('I:compact-disc', GREY),
    '/sidebar/playlist.svg':     ('R:playlist', GREY),
    '/sidebar/mtp.svg':          ('I:usb', GREY),
    '/sidebar/network.svg':      ('R:network', GREY),
    '/sidebar/movie.svg':        ('R:video', GREY),
    '/sidebar/podcast.svg':      ('I:podcast', GREY),
    '/sidebar/music.svg':        ('I:music-double-note', GREY),
    '/sidebar/pictures.svg':     ('I:media-image', GREY),
    '/sidebar/screen.svg':       ('I:computer', GREY),
    '/sidebar/library.svg':      ('I:album-list', GREY),
    '/sidebar/capture.svg':      ('I:video-camera', GREY),
    '/sidebar/lan.svg':          ('I:network', GREY),
    # add-ons manager
    '/addons/default.svg':       ('I:puzzle', GREY),
    '/addons/broken.svg':        ('I:warning-triangle', GREY),
    '/addons/score.svg':         ('I:star', ACCENT),
}
# Kept on purpose: logos, third-party service logos (/sidebar/sd), album art
# placeholders, layout previews, the wait spinner, add-on category colours
# and the classic volume slider masks.


def tinted(svg, colour):
    svg = svg.replace('currentColor', colour)
    # redesign glyphs are authored in black
    svg = re.sub(r'(stroke|fill)="#000(000)?"', lambda m: '%s="%s"' % (m.group(1), colour), svg)
    return svg


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip())
    pkg = sys.argv[1]
    iconoir = os.path.join(pkg, 'icons', 'regular')
    # Validate every input before touching the output directory, so a wrong
    # path never leaves the tree without its committed icons.
    missing = [p for p in (os.path.join(pkg, 'LICENSE'), iconoir) if not os.path.exists(p)]
    for src, _ in MAP.values():
        kind, name = src.split(':')
        f = (os.path.join(iconoir, name + '.svg') if kind == 'I'
             else os.path.join(ROOT, 'pixmaps', 'redesign', name + '.svg'))
        if not os.path.isfile(f):
            missing.append(f)
    if missing:
        sys.exit('not an Iconoir package (npm pack iconoir@7.12.1), missing:\n  '
                 + '\n  '.join(sorted(set(missing))[:10]))
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    shutil.copy(os.path.join(sys.argv[1], 'LICENSE'), os.path.join(OUT, 'ICONOIR-LICENSE'))

    prefixes = {}
    for path, (src, colour) in sorted(MAP.items()):
        kind, name = src.split(':')
        srcfile = (os.path.join(iconoir, name + '.svg') if kind == 'I'
                   else os.path.join(ROOT, 'pixmaps', 'redesign', name + '.svg'))
        svg = tinted(open(srcfile).read(), colour)
        prefix, alias = os.path.split(path)
        prefix = prefix or '/'
        flat = (prefix.strip('/').replace('/', '_') + '_' if prefix != '/' else '') + alias
        dest = os.path.join(OUT, flat)
        if alias.endswith('.png'):
            tmp = dest[:-4] + '.svg'
            open(tmp, 'w').write(svg)
            subprocess.run(['magick', '-background', 'none', '-density', '384', tmp,
                            '-resize', '128x128', dest], check=True)
            os.remove(tmp)
        else:
            open(dest, 'w').write(svg)
        prefixes.setdefault(prefix, []).append((alias, 'pixmaps/modern/' + flat))

    with open(os.path.join(ROOT, 'modern.qrc'), 'w') as q:
        q.write('<!DOCTYPE RCC><RCC version="1.0">\n'
                '<!-- generated by tools/redesign/gen_modern_icons.py, do not edit -->\n')
        for prefix in sorted(prefixes):
            q.write('    <qresource prefix="%s">\n' % prefix)
            for alias, f in prefixes[prefix]:
                q.write('        <file alias="%s">%s</file>\n' % (alias, f))
            q.write('    </qresource>\n')
        q.write('</RCC>\n')

    with open(os.path.join(OUT, 'files.am'), 'w') as am:
        am.write('# generated by tools/redesign/gen_modern_icons.py\nlibqt_plugin_la_MODERN_RES = \\\n')
        files = sorted(f for p in prefixes.values() for _, f in p)
        am.write(' \\\n'.join('\tgui/qt/' + f for f in files) + '\n')
    print(len(MAP), 'icons written to', OUT)


if __name__ == '__main__':
    main()
