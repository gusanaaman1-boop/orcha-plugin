#!/usr/bin/env bash
#
# Assembles the Windows delivery zip:
#
#   dist/ORCHA-<ver>-windows-src.zip   the source, with a .bat that builds,
#                                      tests and installs itself
#
# There is no cross-compiler on this machine and pretending otherwise wastes a
# round trip, so Windows ships as source that builds itself and proves itself
# with the same test suite before it installs anything.

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION="${1:-$(git describe --tags --always 2>/dev/null || echo dev)}"
DIST="$ROOT/dist"

echo "ORCHA packaging — version $VERSION"

# The bundle is produced with `git archive HEAD`, so anything not committed is
# silently absent from it. Refuse rather than ship a tree that differs from
# what was tested.
if ! git diff-index --quiet HEAD -- 2>/dev/null; then
    echo
    echo "REFUSING: the working tree has uncommitted changes."
    echo "The bundle is built from HEAD, so those changes would NOT ship."
    echo "Commit first."
    echo
    git status --short
    exit 1
fi

mkdir -p "$DIST"

echo
echo "== Windows: source bundle =="
STAGE="$DIST/stage-win"
SRC="$STAGE/ORCHA-$VERSION-src"
rm -rf "$STAGE"
mkdir -p "$SRC"

# git archive gives exactly what is tracked - no build folders, no outputs, no
# 200 MB of JUCE.
git archive HEAD | tar -x -C "$SRC"

# The FOUR COLOR delivery set, at the bundle root: one .bat that checks,
# builds, installs and verifies as administrator, reusing the shared
# %USERPROFILE%\JUCE so nothing downloads twice.
cp "$ROOT/packaging/README-WINDOWS.txt" "$SRC/"
cp "$ROOT/packaging/INSTALL-ORCHA-BUILD.bat" "$SRC/"
cp "$ROOT/packaging/UNINSTALL-ORCHA.bat" "$SRC/"

ZIP="$DIST/ORCHA-$VERSION-windows-src.zip"
rm -f "$ZIP"
( cd "$STAGE" && zip -qr "$ZIP" "ORCHA-$VERSION-src" )
rm -rf "$STAGE"

( cd "$DIST" && shasum -a 256 "$(basename "$ZIP")" > SHA256SUMS.txt )

echo
echo "== done =="
ls -lh "$DIST" | tail -n +2
