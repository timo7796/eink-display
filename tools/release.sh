#!/bin/zsh
# Baut die Firmware und veroeffentlicht sie als GitHub-Release.
# Alle Geraete mit passender OTA_MANIFEST_URL installieren sie dann automatisch.
#
# Aufruf:  tools/release.sh <github-user>/<repo>
# Vorher:  FIRMWARE_VERSION in include/config.h hochzaehlen.
# Voraussetzung: gh CLI angemeldet (gh auth login), Repo ist oeffentlich -
# die Geraete laden ohne Login.
set -e
cd "$(dirname "$0")/.."

REPO=${1:?"Aufruf: tools/release.sh <github-user>/<repo>"}
VERSION=$(sed -n 's/^#define FIRMWARE_VERSION "\(.*\)"/\1/p' include/config.h)
PIO=$(command -v pio || echo ~/.platformio/penv/bin/pio)

echo "Baue Firmware $VERSION ..."
"$PIO" run

mkdir -p release
cp .pio/build/nodemcu-32s/firmware.bin release/firmware.bin
cat > release/version.json <<JSON
{"version": "$VERSION", "url": "https://github.com/$REPO/releases/download/v$VERSION/firmware.bin"}
JSON

gh release create "v$VERSION" release/firmware.bin release/version.json \
  --repo "$REPO" --title "v$VERSION" --notes "Firmware $VERSION"

echo "Fertig. Manifest: https://github.com/$REPO/releases/latest/download/version.json"
