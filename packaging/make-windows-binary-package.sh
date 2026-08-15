#!/usr/bin/env bash
# Assembles the WhatsApp-ready Windows ZIP: a prebuilt plugin plus the simple
# copy installer. This is the bundle for musicians - the source bundle built by
# make-packages.sh is the one for developers.
#
#   packaging/make-windows-binary-package.sh <path-to-ORCHA.vst3> [ORCHA.exe]
#
# The .vst3 must be the Windows FOLDER bundle, the one containing
# Contents/x86_64-win/ORCHA.vst3. It comes from either
#   - the Windows CI job (.github/workflows/windows.yml), or
#   - a machine that ran INSTALL-ORCHA-BUILD.bat once, where it ends up in
#     C:\Program Files\Common Files\VST3\ORCHA.vst3
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
version="$(sed -n 's/^project(Orcha VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"

vst3="${1:-}"
exe="${2:-}"

if [[ -z "$vst3" ]]; then
    echo "usage: $(basename "$0") <path-to-ORCHA.vst3> [ORCHA.exe]" >&2
    exit 2
fi

# Refuse to ship something that is not what it claims to be. A macOS .vst3 or
# an empty folder would sail through a naive copy and fail on the user's
# machine, which is the worst place to find out.
if [[ ! -d "$vst3" ]]; then
    echo "ERROR: $vst3 is not a folder. The Windows VST3 is a folder bundle." >&2
    exit 1
fi
binary="$vst3/Contents/x86_64-win/ORCHA.vst3"
if [[ ! -f "$binary" ]]; then
    echo "ERROR: no Windows binary at Contents/x86_64-win/ORCHA.vst3 inside" >&2
    echo "       $vst3" >&2
    echo "       That path only exists in a WINDOWS build. A macOS bundle has" >&2
    echo "       Contents/MacOS instead - this script will not repackage one." >&2
    exit 1
fi
if ! file "$binary" | grep -qi "PE32+\|MS Windows\|x86-64"; then
    echo "WARNING: $binary does not look like a Windows PE binary:" >&2
    file "$binary" >&2
    echo "         Refusing to package it. Check where this build came from." >&2
    exit 1
fi

stage="$(mktemp -d)"
out="ORCHA-$version-windows"
mkdir -p "$stage/$out"

cp -R "$vst3" "$stage/$out/ORCHA.vst3"
cp "$here/INSTALL-ORCHA.bat"        "$stage/$out/"
cp "$here/UNINSTALL-ORCHA.bat"      "$stage/$out/"
cp "$here/README-ORCHA-WINDOWS.txt" "$stage/$out/README.txt"

if [[ -n "$exe" && -f "$exe" ]]; then
    cp "$exe" "$stage/$out/ORCHA.exe"
    echo "  including the standalone app"
fi

# CRLF: these files are read in Notepad on Windows, where a lone LF is one
# long unreadable line.
for f in "$stage/$out/README.txt" "$stage/$out"/*.bat; do
    perl -pi -e 's/\r?\n/\r\n/' "$f"
done

mkdir -p "$root/dist"
zip_path="$root/dist/$out.zip"
rm -f "$zip_path"
( cd "$stage" && zip -qr "$zip_path" "$out" )
rm -rf "$stage"

size="$(du -h "$zip_path" | cut -f1)"
echo "== done =="
echo "  $zip_path  ($size)"
shasum -a 256 "$zip_path"
echo
echo "  WhatsApp caps documents at 2 GB, so size is not a problem."
echo "  Tell people: extract the ZIP, then right-click INSTALL-ORCHA.bat"
echo "  and Run as administrator. It is unsigned, so Windows shows a blue"
echo "  SmartScreen box - More info, then Run anyway."
