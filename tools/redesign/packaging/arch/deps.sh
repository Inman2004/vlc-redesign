#!/bin/bash
# deps.sh PKGDIR -- list the Arch packages that own the libraries the build links to
# (run after one `makepkg -o -e` + build, or on an installed tree)
find "$1" \( -name '*.so*' -o -path '*/bin/*' \) -type f -exec ldd {} + 2>/dev/null \
  | awk '/=> \//{print $3}' | sort -u | grep -v "$1" \
  | xargs -r pacman -Qoq 2>/dev/null | sort -u | tr '\n' ' '
