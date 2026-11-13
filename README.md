# VLC Redesign

A redesigned Qt interface for **VLC media player 3.0.24**, brought to life
from **[Juxtopposed](https://www.youtube.com/@juxtopposed)'s** *VLC Redesign*
— her video [*I Redesigned VLC After 25 Years*](https://youtu.be/ERFeOgqk324)
and her [Figma Community file](https://www.figma.com/community/file/4UaxYzCN8lGeBI9uEAeWhN).
The whole player is VLC 3.0.24 — the same decoders, formats and settings —
with her interface: frameless window, library, music mode, queue, settings
and dialogs, set in Google Sans.

Built by **[Immanuvel B](https://github.com/Inman2004)**
([rvinmandev.vercel.app](https://rvinmandev.vercel.app)).

> **Unofficial.** This is an independent project. It isn't made, endorsed or
> supported by VideoLAN, and Juxtopposed isn't involved in this build.
> For the official player, see [videolan.org](https://www.videolan.org/vlc/).

![The player: header, navigation rail, playlist sidebar and one-row control bar](docs/screenshots/player.png)

## The design is Juxtopposed's

Every screen here is Juxtopposed's work. In
[**I Redesigned VLC After 25 Years**](https://youtu.be/ERFeOgqk324) she
reimagines VLC's interface. She keeps what makes VLC recognisable: the
traffic cone, now the volume control, and the warm dark look. Around that
she modernises everything:

- **A cleaner player:** a frameless window and a single control bar.
- **A library:** a library, a music mode and a queue.
- **Tools:** a subtitle timeline, a Customize Player editor, and a
  reorganised settings window.

She published the full design as a
[**Figma Community file**](https://www.figma.com/community/file/4UaxYzCN8lGeBI9uEAeWhN):
every screen, state, icon, colour and measurement. This project exists
because of that generosity. Her video explains the reasoning; her Figma
file is the specification this code follows, down to the pixel and her own
icon vectors.

All of the UI and UX thinking is hers; the implementation in VLC is ours,
and so are any mistakes. If you like what you see here, go watch the video
and follow her work:

- YouTube: [@juxtopposed](https://www.youtube.com/@juxtopposed)
- Video: [I Redesigned VLC After 25 Years](https://youtu.be/ERFeOgqk324)
- Figma: [VLC Redesign](https://www.figma.com/community/file/4UaxYzCN8lGeBI9uEAeWhN)

## Author

**Immanuvel B** built this, turning Juxtopposed's design into a working
VLC: the Qt interface, the Linux package and the Windows installers.

- GitHub: [@Inman2004](https://github.com/Inman2004)
- Website: [rvinmandev.vercel.app](https://rvinmandev.vercel.app)

## Download

Get the latest build from [**Releases**](../../releases).

| Platform | File | Notes |
|---|---|---|
| Windows 10 / 11 (64-bit) | `vlc-redesign-…-win64-setup.exe` | Installer |
| Windows 10 / 11 (64-bit) | `vlc-redesign-…-win64.zip` | Portable: unzip and run `vlc.exe` |
| Arch Linux / CachyOS (x86_64) | `vlc-redesign-…-x86_64.pkg.tar.zst` | `sudo pacman -U <file>`, then run `vlc-redesign` |

Each release lists SHA-256 checksums in `SHA256SUMS`.

**Before installing on Windows**

- **The installer replaces an installed official VLC.** It still installs
  as "VLC media player", in the same folder. To keep the official player,
  use the portable zip instead; it runs from any folder.
- **The builds aren't code-signed**, so SmartScreen warns on first run.
  Choose *More info → Run anyway* if you trust the download.
- **Windows builds are cross-compiled from Linux.** See
  [Known limitations](#known-limitations).

The Arch package installs to `/opt/vlc-redesign` and leaves the system's
`vlc` alone.

## What's new

| Area | What changed |
|---|---|
| Player | Frameless window with its own header (menu, library toggle, folder, title, window buttons); navigation rail; one-row control bar with an accent play pill, a 28 px seek bar and a volume cone |
| Fullscreen | Auto-hiding top strip and control bar; seek-bar preview thumbnails |
| Menus | Main menu with shortcuts and profiles; Video / Audio / Subtitles track popover; item, sort and view menus |
| Adjustments | Four-tab side panel: video, audio, subtitles and playback |
| Dialogs | Open Network Stream, New Playlist, Add Subtitles (folder, link or OpenSubtitles search) |
| Library | Folder scan with thumbnails, Home with a resume card, Recent, browse and list views, a mini player |
| Music | Song tags and cover art, music categories, a music-mode bar with a waveform seek bar |
| Playlists and queue | Named playlists; Queue / Bookmarks / Chapters panel; drag to the queue |
| Settings | Settings window with 13 pages; Keyboard Shortcuts dialog where keys apply without a restart |
| Subtitles | Optional subtitle timeline on the seek bar; `Ctrl+B` quick bookmark; bookmarks kept per file |
| Look | Dark, light or system theme; nine accent colours |

<table>
  <tr>
    <td><img src="docs/screenshots/main-menu.png" alt="Main menu: build on the left, Figma design on the right"></td>
    <td><img src="docs/screenshots/track-popover.png" alt="Track popover: build on the left, Figma design on the right"></td>
  </tr>
  <tr>
    <td align="center"><sub>Main menu — build (left) and Figma (right)</sub></td>
    <td align="center"><sub>Track popover — build (left) and Figma (right)</sub></td>
  </tr>
</table>

![Subtitle timeline: Figma design on top, build below](docs/screenshots/subtitle-timeline.png)
<sub>Subtitle timeline — Figma (top) and build (bottom)</sub>

## Options

Everything is in **Settings**; these are the same options from the command
line or `vlcrc`.

| Option | Default | What it does |
|---|---|---|
| `--qt-redesign` / `--no-qt-redesign` | on | The redesigned interface; off gives VLC's classic Qt interface |
| `--qt-redesign-frameless` | on | Frameless window with the custom header; off keeps the system title bar |
| `--qt-theme` | `dark` | `dark`, `light` or `system` |
| `--qt-accent-color` | `#D37918` | Amber, Rust, Olive, Green, Teal, Blue, Violet, Magenta or Grey |
| `--qt-subtitle-timeline` | off | Subtitle blocks on the seek bar while a subtitle file plays |
| `--qt-key-quick-bookmark` | `Ctrl+B` | Bookmark the current position |
| `--qt-library-roots` | Videos and Music | Folders the library scans, separated by `;` |

## Building from source

The interface lives in `modules/gui/qt/`. Everything else is VLC 3.0.24.

**Linux (development build)**

```bash
./bootstrap
./configure --enable-qt
make -j"$(nproc)"
./vlc
```

**Arch Linux package**

```bash
cd tools/redesign/packaging/arch
makepkg -si          # -s installs the build dependencies, -i the package
```

**Windows (cross-compiled from Linux)**

Needs `mingw-w64-gcc`, `mingw-w64-tools` and `nsis`, plus VideoLAN's
prebuilt win64 contribs unpacked in `contrib/x86_64-w64-mingw32`.

```bash
tools/redesign/packaging/windows/build.sh all
```

That script also explains the two build-time shims it links: VideoLAN's
contribs ship Qt 5.6 and were built for the older msvcrt runtime.
`modules/gui/qt/util/qt_compat.*` keeps the interface building on Qt 5.6
and 5.15.

## Known limitations

- **Windows builds haven't been tested on Windows yet.** They're
  cross-compiled and checked statically: 64-bit binaries that import only
  Windows system DLLs.
- **Windows: no online subtitle search.** VideoLAN's Windows Qt has no
  network module. Adding subtitles from a folder or a link works.
- **Windows: no SRT streaming and English only.** The prebuilt SRT library
  doesn't link with the current toolchain, and the interface is built
  without translations.
- **Customize Player** (drag-and-drop layout editor) is in progress and not
  in these builds.

## Credits and licences

- **VLC media player** — © the VideoLAN team and contributors,
  GPL-2.0-or-later / LGPL-2.1-or-later. See [`COPYING`](COPYING),
  [`COPYING.LIB`](COPYING.LIB) and [`AUTHORS`](AUTHORS); VLC's own readme
  is [`README`](README).
- **The redesign in VLC** — [Immanuvel B](https://github.com/Inman2004).
- **UI and UX design** — *VLC Redesign* by
  [Juxtopposed](https://www.youtube.com/@juxtopposed): the video
  [*I Redesigned VLC After 25 Years*](https://youtu.be/ERFeOgqk324) and the
  [Figma Community file](https://www.figma.com/community/file/4UaxYzCN8lGeBI9uEAeWhN).
  Icons come from her Figma vectors wherever the file has them.
- **Google Sans** — SIL Open Font License 1.1
  ([`OFL.txt`](modules/gui/qt/pixmaps/redesign/fonts/OFL.txt)).
- **Iconoir** icons, used where the Figma file has none — MIT
  ([`ICONOIR-LICENSE`](modules/gui/qt/pixmaps/modern/ICONOIR-LICENSE)).

This is a **modified version of VLC 3.0.24**; what changed, who changed
it and when is summarised in
[`tools/redesign/packaging/NOTICE.txt`](tools/redesign/packaging/NOTICE.txt),
which ships in every package with the font and icon licences. The git
history is the full record.

"VLC", "VLC media player", "VideoLAN" and the cone are trademarks of
VideoLAN. They're used here only to name the software this is built on, as
[VideoLAN's legal page](https://www.videolan.org/legal.html) allows for
redistributions that comply with the GPL.
