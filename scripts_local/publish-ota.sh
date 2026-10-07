#!/bin/bash
# Build the x4pro_ota env and publish it to a personal OTA feed in a Google
# Cloud Storage bucket, so Settings > Check for Updates on the device finds it.
#
#   ./scripts_local/publish-ota.sh             # build, then upload
#   ./scripts_local/publish-ota.sh --dry-run   # build and write latest.json only
#
# Reads from the environment, never from git:
#   CROSSPLAY_OTA_BUCKET   bucket name
#   CROSSPLAY_OTA_PREFIX   unguessable path inside it (the feed's only secrecy)
#   CROSSPLAY_OTA_GCS_KEY  base64 of a service-account key allowed to write there
# Any extra -D flags (the Google client for Tasks/Calendar) go in
# PLATFORMIO_BUILD_FLAGS as usual.
#
# The version is the UTC build time as YYYY.MDD.HMM, three numbers that only
# grow, which is all OtaUpdater::isUpdateNewer compares.
set -euo pipefail

DRY_RUN=0
[[ "${1:-}" == "--dry-run" ]] && DRY_RUN=1

: "${CROSSPLAY_OTA_BUCKET:?set CROSSPLAY_OTA_BUCKET}"
: "${CROSSPLAY_OTA_PREFIX:?set CROSSPLAY_OTA_PREFIX}"
[[ $DRY_RUN == 1 ]] || : "${CROSSPLAY_OTA_GCS_KEY:?set CROSSPLAY_OTA_GCS_KEY}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASE="https://storage.googleapis.com/${CROSSPLAY_OTA_BUCKET}/${CROSSPLAY_OTA_PREFIX}"
VERSION="$(date -u +%Y.%-m%d.%-H%M)"
export CROSSPLAY_OTA_VERSION="$VERSION"
export CROSSPLAY_OTA_URL="${BASE}/latest.json"

cd "$ROOT"
pio run -e x4pro_ota
BIN="$ROOT/.pio/build/x4pro_ota/firmware.bin"
grep -qa "$VERSION" "$BIN" || { echo "version $VERSION is not in $BIN" >&2; exit 1; }
grep -qa "$CROSSPLAY_OTA_URL" "$BIN" || { echo "feed URL is not in $BIN" >&2; exit 1; }

OUT="$(mktemp -d)"
ASSET="firmware-${VERSION}.bin"
cp "$BIN" "$OUT/$ASSET"
SIZE="$(stat -c %s "$BIN" 2>/dev/null || stat -f %z "$BIN")"
# The shape ReleaseJsonParser reads from GitHub: tag_name plus an asset named
# firmware.bin. The object behind it is versioned so a device mid-download
# never sees it swapped.
cat >"$OUT/latest.json" <<EOF
{"tag_name":"v${VERSION}","assets":[{"name":"firmware.bin","browser_download_url":"${BASE}/${ASSET}","size":${SIZE}}]}
EOF
echo "built v${VERSION} (${SIZE} bytes) in $OUT"
[[ $DRY_RUN == 1 ]] && exit 0

KEY="$OUT/key.json"
trap 'rm -f "$KEY"' EXIT
echo "$CROSSPLAY_OTA_GCS_KEY" | base64 -d >"$KEY"
export CLOUDSDK_CONFIG="$OUT/gcloud"
gcloud auth activate-service-account --key-file="$KEY" --quiet
# Firmware first, feed last, so the feed never points at a missing object.
gcloud storage cp "$OUT/$ASSET" "gs://${CROSSPLAY_OTA_BUCKET}/${CROSSPLAY_OTA_PREFIX}/${ASSET}"
gcloud storage cp --cache-control="no-store" --content-type="application/json" \
  "$OUT/latest.json" "gs://${CROSSPLAY_OTA_BUCKET}/${CROSSPLAY_OTA_PREFIX}/latest.json"
echo "published v${VERSION}"
