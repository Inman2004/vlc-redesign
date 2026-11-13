#!/bin/bash
# Cross-compile the redesign for 64-bit Windows from Linux and package it.
#
#   tools/redesign/packaging/windows/build.sh [configure|build|package|all]
#
# Needs: mingw-w64-gcc, mingw-w64-tools (pkg-config), nsis (makensis),
# zstd, and VideoLAN's prebuilt win64 contribs unpacked in
# contrib/x86_64-w64-mingw32 (see the vault note for the download).
# Output: win64/vlc-<version>-win64.zip and .exe in the build folder.
set -euo pipefail

SRC=$(cd "$(dirname "$0")/../../../.." && pwd)
HERE=$SRC/tools/redesign/packaging/windows
HOST=x86_64-w64-mingw32
C=$SRC/contrib/$HOST
B=$SRC/win64
COMPAT=$B/redesign-compat
STEP=${1:-all}

[ -d "$C/lib/pkgconfig" ] || { echo "contribs missing: $C" >&2; exit 1; }

# the contribs' own tools (moc, uic, rcc, protoc...) come first; plain
# pkg-config limited to the contribs (the mingw wrapper adds the system's)
export PATH="$C/bin:$PATH"
export PKG_CONFIG="pkg-config --static"
export PKG_CONFIG_LIBDIR="$C/lib/pkgconfig"
unset PKG_CONFIG_PATH

compat() {
    # msvcrt and libebml entry points the prebuilt contribs expect
    mkdir -p "$COMPAT"
    $HOST-gcc -O2 -c "$HERE/crt_compat.c" -o "$COMPAT/crt_compat.o"
    $HOST-gcc -c "$HERE/ebml_compat.S" -o "$COMPAT/ebml_compat.o"
    rm -f "$COMPAT/libredesigncompat.a"
    $HOST-ar rcs "$COMPAT/libredesigncompat.a" "$COMPAT/crt_compat.o" "$COMPAT/ebml_compat.o"
}

configure() {
    compat
    mkdir -p "$B"; cd "$B"
    # GCC 16 defaults to C23, which dropped ATOMIC_VAR_INIT; Windows 7 API
    # level keeps WaitOnAddress (Windows 8) out of the build
    CFLAGS="-O2 -std=gnu17" CXXFLAGS="-O2" CPPFLAGS="-D_WIN32_WINNT=0x0601" \
    LIBS="-L$COMPAT -lredesigncompat -L$C/lib -lebml" \
    ../extras/package/win32/configure.sh --host=$HOST --with-contrib=../contrib/$HOST \
        --enable-qt --disable-skins2 --disable-nls --disable-update-check \
        --disable-srt
}

build() {
    compat
    cd "$B"
    make -k -j"$(nproc)"
}

package() {
    cd "$B"
    make package-win-strip
    make package-win32-zip
    make package-win32-exe
    ls -la vlc-*-win64.* 2>/dev/null || ls -la ./*.zip ./*.exe
}

case "$STEP" in
    configure) configure ;;
    build)     build ;;
    package)   package ;;
    all)       configure; build; package ;;
    *) echo "usage: $0 [configure|build|package|all]" >&2; exit 2 ;;
esac
