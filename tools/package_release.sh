#!/usr/bin/env bash
# Packages a built firmware (run `pio run` first) into release files and the
# browser-flasher site. Used by .github/workflows/release.yml; also works locally.
#
#   dist/  M5_NightscoutBedside-<ver>-full.bin  whole flash image, write at 0x0
#          M5_NightscoutBedside-<ver>-ota.bin   app only, for Wi-Fi updates
#          M5NS.INI                             example settings file
#          NOTES.md                             this version's CHANGELOG section
#   site/  index.html + manifest.json + full.bin  (ESP Web Tools flasher page)
set -euo pipefail
cd "$(dirname "$0")/.."

NAME=M5_NightscoutBedside
VERSION=$(sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p' include/version.h)
BUILD=.pio/build/m5stack-basic
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
FULL="$NAME-$VERSION-full.bin"

rm -rf dist site
mkdir -p dist site

# One image for the whole flash: bootloader, partition table, OTA data, app.
pio pkg exec --package platformio/tool-esptoolpy -- esptool.py --chip esp32 merge_bin \
  -o "dist/$FULL" --flash_mode keep --flash_freq keep --flash_size 4MB \
  0x1000 "$BUILD/bootloader.bin" 0x8000 "$BUILD/partitions.bin" \
  0xe000 "$BOOT_APP0" 0x10000 "$BUILD/firmware.bin"
cp "$BUILD/firmware.bin" "dist/$NAME-$VERSION-ota.bin"
cp SD/M5NS.INI dist/M5NS.INI

# Release notes: this version's section of CHANGELOG.md.
awk -v v="## $VERSION" 'index($0, v) == 1 {on = 1; next} /^## / {on = 0} on' CHANGELOG.md > dist/NOTES.md
[ -s dist/NOTES.md ] || { echo "No CHANGELOG.md section for $VERSION" >&2; exit 1; }

# Flasher site.
cp "dist/$FULL" site/
cp SD/M5NS.INI site/M5NS.INI
cp docs/clock.png site/clock.png
sed -e "s/{{VERSION}}/$VERSION/g" web/index.html > site/index.html
cat > site/manifest.json <<EOF
{
  "name": "$NAME",
  "version": "$VERSION",
  "new_install_prompt_erase": true,
  "builds": [
    { "chipFamily": "ESP32", "parts": [{ "path": "$FULL", "offset": 0 }] }
  ]
}
EOF

echo "Packaged $NAME $VERSION:"
ls -l dist site
