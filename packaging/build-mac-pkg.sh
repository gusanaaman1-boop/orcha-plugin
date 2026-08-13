#!/bin/bash
# Packages the Release build into dist/ORCHA-<version>-macOS.zip: a real .pkg
# installer for VST3 + AU + Standalone, plus the install README. Same layout
# as the FOUR COLOR / TRIX macOS packages - a .pkg because ad-hoc-signed
# bundles copied by hand fail Gatekeeper on OTHER Macs once quarantined.
set -euo pipefail

VERSION="${1:-$(git -C "$(dirname "$0")/.." describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || echo dev)}"
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
ART="$PROJ/build/Orcha_artefacts/Release"
STAGE="$(mktemp -d)"
DIST="$PROJ/dist"
mkdir -p "$DIST"

[ -d "$ART/VST3/ORCHA.vst3" ]      || { echo "VST3 not built - run the Release build first"; exit 1; }
[ -d "$ART/AU/ORCHA.component" ]   || { echo "AU not built - run the Release build first"; exit 1; }
[ -d "$ART/Standalone/ORCHA.app" ] || { echo "Standalone not built - run the Release build first"; exit 1; }

# Every slice must actually be universal - an arm64-only build looks identical
# until someone opens it under Rosetta.
for f in "VST3/ORCHA.vst3/Contents/MacOS/ORCHA" \
         "AU/ORCHA.component/Contents/MacOS/ORCHA" \
         "Standalone/ORCHA.app/Contents/MacOS/ORCHA"; do
    archs=$(lipo -archs "$ART/$f")
    case "$archs" in
        *x86_64*arm64*|*arm64*x86_64*) ;;
        *) echo "NOT UNIVERSAL: $f ($archs)"; exit 1;;
    esac
done

# Stage payloads. COPYFILE_DISABLE stops AppleDouble ._ files landing inside
# the installer payload, where they show up as junk in the BOM.
mkdir -p "$STAGE/vst3" "$STAGE/au" "$STAGE/app"
export COPYFILE_DISABLE=1
cp -R "$ART/VST3/ORCHA.vst3"      "$STAGE/vst3/"
cp -R "$ART/AU/ORCHA.component"   "$STAGE/au/"
cp -R "$ART/Standalone/ORCHA.app" "$STAGE/app/"
xattr -cr "$STAGE/vst3/ORCHA.vst3" "$STAGE/au/ORCHA.component" "$STAGE/app/ORCHA.app"
find "$STAGE" -name "._*" -delete

# Ad-hoc signature so the bundles load cleanly on another Mac. This is NOT
# notarisation - the README explains the right-click -> Open step.
codesign --force --deep -s - "$STAGE/vst3/ORCHA.vst3"
codesign --force --deep -s - "$STAGE/au/ORCHA.component"
codesign --force --deep -s - "$STAGE/app/ORCHA.app"

# Signing leaves extended attributes behind; clean again so the BOM holds only
# the real bundles.
xattr -cr "$STAGE/vst3" "$STAGE/au" "$STAGE/app"
find "$STAGE" -name "._*" -delete

pkgbuild --root "$STAGE/vst3" \
         --identifier com.naaman.orcha.vst3 --version "$VERSION" \
         --install-location /Library/Audio/Plug-Ins/VST3 \
         "$STAGE/Orcha-VST3.pkg" > /dev/null

pkgbuild --root "$STAGE/au" \
         --identifier com.naaman.orcha.au --version "$VERSION" \
         --install-location /Library/Audio/Plug-Ins/Components \
         "$STAGE/Orcha-AU.pkg" > /dev/null

pkgbuild --root "$STAGE/app" \
         --identifier com.naaman.orcha.app --version "$VERSION" \
         --install-location /Applications \
         "$STAGE/Orcha-App.pkg" > /dev/null

sed "s/@VERSION@/$VERSION/g" "$PROJ/packaging/Distribution.xml" > "$STAGE/Distribution.xml"

productbuild --distribution "$STAGE/Distribution.xml" \
             --package-path "$STAGE" \
             "$STAGE/ORCHA-$VERSION-macOS.pkg" > /dev/null

cp "$PROJ/packaging/README-INSTALL-MAC.txt" "$STAGE/"
( cd "$STAGE" && zip -q -X "ORCHA-$VERSION-macOS.zip" \
      "ORCHA-$VERSION-macOS.pkg" README-INSTALL-MAC.txt )
mv "$STAGE/ORCHA-$VERSION-macOS.zip" "$DIST/"
rm -rf "$STAGE"

echo "wrote $DIST/ORCHA-$VERSION-macOS.zip"
