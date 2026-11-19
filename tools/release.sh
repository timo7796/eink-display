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
"$PIO" run -e nodemcu-32s -e esp32-s3

mkdir -p release
URL="https://github.com/$REPO/releases/download/v$VERSION"
# Normaler ESP32
cp .pio/build/nodemcu-32s/firmware.bin release/firmware.bin
echo "{\"version\": \"$VERSION\", \"url\": \"$URL/firmware.bin\"}" > release/version.json
# ESP32-S3
cp .pio/build/esp32-s3/firmware.bin release/firmware-s3.bin
echo "{\"version\": \"$VERSION\", \"url\": \"$URL/firmware-s3.bin\"}" > release/version-s3.json

gh release create "v$VERSION" release/firmware.bin release/version.json \
  release/firmware-s3.bin release/version-s3.json \
  --repo "$REPO" --title "v$VERSION" --notes "Firmware $VERSION"

echo "Fertig. Manifest: https://github.com/$REPO/releases/latest/download/version.json"
