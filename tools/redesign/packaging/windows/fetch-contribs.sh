#!/bin/bash
# Download and unpack VideoLAN's prebuilt win64 contribs into a source tree.
#
#   tools/redesign/packaging/windows/fetch-contribs.sh SRC_TREE [CACHE_DIR]
#
# The server is slow per connection, so the archive comes down in parallel
# byte ranges over HTTP/1.1 (HTTP/2 streams get reset), resumes, checks
# every piece's size and tests the archive before unpacking it into
# SRC_TREE/contrib/x86_64-w64-mingw32.
set -euo pipefail

# the contrib hash VLC 3.0.24 expects (extras/ci/get-contrib-sha.sh: the
# last commit touching contrib, extras/tools, extras/ci or extras/package/win32)
HASH=910cc9cada6080dd777fbafb2ebdd0c24f702c30
URL=https://artifacts.videolan.org/vlc-3.0/win64/vlc-contrib-x86_64-w64-mingw32-$HASH.tar.zst
PARTS=16

SRC=$(cd "${1:?usage: $0 SRC_TREE [CACHE_DIR]}" && pwd)
CACHE=${2:-$HOME/.local/share/vlc-redesign/contrib-dl}
ARCHIVE=$CACHE/contrib-$HASH.tar.zst
mkdir -p "$CACHE"

if ! zstd -q -t "$ARCHIVE" 2>/dev/null; then
    size=$(curl -sI --http1.1 "$URL" | awk 'tolower($1)=="content-length:"{print $2+0}')
    [ "${size:-0}" -gt 0 ] || { echo "can't read the archive size" >&2; exit 1; }
    chunk=$(( (size + PARTS - 1) / PARTS ))
    echo "downloading $size bytes in $PARTS parts"
    for i in $(seq 0 $((PARTS - 1))); do
        start=$(( i * chunk )); end=$(( start + chunk - 1 ))
        [ $end -ge $size ] && end=$(( size - 1 ))
        want=$(( end - start + 1 ))
        part=$CACHE/part.$i
        (
            for try in 1 2 3 4 5 6 7 8; do
                have=$(stat -c %s "$part" 2>/dev/null || echo 0)
                [ "$have" -eq "$want" ] && exit 0
                [ "$have" -gt "$want" ] && { rm -f "$part"; have=0; }
                curl -s --http1.1 --retry 3 -r $(( start + have ))-$end "$URL" >> "$part" || true
            done
            echo "part $i incomplete" >&2; exit 1
        ) &
    done
    wait
    for i in $(seq 0 $((PARTS - 1))); do
        [ -f "$CACHE/part.$i" ] || { echo "part $i missing" >&2; exit 1; }
    done
    for i in $(seq 0 $((PARTS - 1))); do cat "$CACHE/part.$i"; done > "$ARCHIVE"
    [ "$(stat -c %s "$ARCHIVE")" -eq "$size" ] || { echo "size mismatch" >&2; exit 1; }
    zstd -q -t "$ARCHIVE" || { echo "archive is corrupt" >&2; exit 1; }
    rm -f "$CACHE"/part.*
fi

mkdir -p "$SRC/contrib"
tar -C "$SRC/contrib" -I zstd -xf "$ARCHIVE"
# the archive carries @@CONTRIB_PREFIX@@ placeholders: point them here
(cd "$SRC/contrib/x86_64-w64-mingw32" && "$SRC/contrib/src/change_prefix.sh")
ls -d "$SRC/contrib/x86_64-w64-mingw32/lib/pkgconfig" >/dev/null

# a native luac that writes the contrib Lua's bytecode (contrib/src/lua:
# Lua 5.1.5 with luac-32bits.patch), where configure looks for it
LUAC=$SRC/contrib/bin/x86_64-w64-mingw32-luac
if [ ! -x "$LUAC" ]; then
    LUA_TGZ=$CACHE/lua-5.1.5.tar.gz
    [ -f "$LUA_TGZ" ] || curl -sfL -o "$LUA_TGZ" https://www.lua.org/ftp/lua-5.1.5.tar.gz
    want=$(awk '$2=="lua-5.1.5.tar.gz"{print $1}' "$SRC/contrib/src/lua/SHA512SUMS")
    [ "$(sha512sum "$LUA_TGZ" | cut -d' ' -f1)" = "$want" ] || { echo "lua tarball checksum mismatch" >&2; exit 1; }
    work=$(mktemp -d)
    tar -C "$work" -xzf "$LUA_TGZ" --strip-components=1
    patch -d "$work" -p1 -s < "$SRC/contrib/src/lua/luac-32bits.patch"
    make -s -C "$work" generic CC=cc >/dev/null
    install -Dm755 "$work/src/luac" "$LUAC"
    rm -rf "$work"
fi
echo "contribs ready in $SRC/contrib/x86_64-w64-mingw32 (luac: $LUAC)"
