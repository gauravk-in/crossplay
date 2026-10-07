#!/bin/bash
# Build the x4pro_ota env and publish it as a GitHub release, so Settings >
# Check for Updates on a device running an x4pro_ota build finds it.
#
#   ./scripts_local/publish-ota.sh             # build, then release
#   ./scripts_local/publish-ota.sh --dry-run   # build only
#
# Reads from the environment, never from git:
#   GITHUB_RELEASE_TOKEN   fine-grained token, Contents: read and write
#   CROSSPLAY_OTA_REPO     owner/repo to release to (default gauravk-in/crossplay)
# Any extra -D flags (the Google client for Tasks/Calendar) go in
# PLATFORMIO_BUILD_FLAGS as usual.
#
# The version is the UTC build time as YYYY.MDD.HMM, three numbers that only
# grow, which is all OtaUpdater::isUpdateNewer compares.
set -euo pipefail

DRY_RUN=0
[[ "${1:-}" == "--dry-run" ]] && DRY_RUN=1

REPO="${CROSSPLAY_OTA_REPO:-gauravk-in/crossplay}"
[[ $DRY_RUN == 1 ]] || : "${GITHUB_RELEASE_TOKEN:?set GITHUB_RELEASE_TOKEN}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(date -u +%Y.%-m%d.%-H%M)"
export CROSSPLAY_OTA_VERSION="$VERSION"
export CROSSPLAY_OTA_URL="https://api.github.com/repos/${REPO}/releases/latest"

cd "$ROOT"
pio run -e x4pro_ota
BIN="$ROOT/.pio/build/x4pro_ota/firmware.bin"
grep -qa "$VERSION" "$BIN" || { echo "version $VERSION is not in $BIN" >&2; exit 1; }
grep -qa "$CROSSPLAY_OTA_URL" "$BIN" || { echo "feed URL is not in $BIN" >&2; exit 1; }
if [[ -n "${GTASKS_CLIENT_ID:-}" ]]; then
  grep -qa "$GTASKS_CLIENT_ID" "$BIN" || { echo "Google client id is not in $BIN" >&2; exit 1; }
fi
echo "built v${VERSION} from $(git rev-parse --short HEAD)"
[[ $DRY_RUN == 1 ]] && exit 0

# The release tags HEAD, so HEAD has to exist on GitHub.
git fetch -q origin
git branch -r --contains HEAD | grep -q . || { echo "HEAD is not pushed to origin" >&2; exit 1; }

AUTH=(-H "Authorization: Bearer ${GITHUB_RELEASE_TOKEN}" -H "Accept: application/vnd.github+json")
BODY="$(printf '{"tag_name":"v%s","target_commitish":"%s","name":"v%s","body":"Built from %s.","make_latest":"true"}' \
  "$VERSION" "$(git rev-parse HEAD)" "$VERSION" "$(git rev-parse --short HEAD)")"
RELEASE="$(curl -fsS "${AUTH[@]}" -H "Content-Type: application/json" -X POST "https://api.github.com/repos/${REPO}/releases" -d "$BODY")"
ID="$(printf '%s' "$RELEASE" | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')"
# The x4pro updater asks for the literal asset name firmware.bin
# (CROSSPOINT_RELEASE_ASSET in FirmwareBoardTag.h).
curl -fsS "${AUTH[@]}" -H "Content-Type: application/octet-stream" --data-binary @"$BIN" \
  "https://uploads.github.com/repos/${REPO}/releases/${ID}/assets?name=firmware.bin" >/dev/null
echo "published v${VERSION}: https://github.com/${REPO}/releases/tag/v${VERSION}"
