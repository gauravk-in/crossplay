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

# The release body: every commit since the last release, its subject and the
# first paragraph of its message, so the release page says what changed.
release_notes() {
  local prev range
  prev="$(git tag --merged HEAD --sort=-creatordate --list 'v*' | head -1)"
  if [[ -n "$prev" ]]; then
    range="$prev..HEAD"
    printf 'Changes since %s:\n\n' "$prev"
  else
    range="HEAD~20..HEAD"
    printf 'Recent changes:\n\n'
  fi
  git log --no-merges --reverse --format='%x00%s%n%b' "$range" | python3 -c '
import sys
for entry in sys.stdin.read().split("\0")[1:]:
    subject, _, body = entry.partition("\n")
    print("- **" + subject.strip() + "**")
    para = body.strip().split("\n\n")[0].strip()
    if para and not para.startswith(("Co-Authored-By", "Claude-Session")):
        print("  " + " ".join(para.split()))
'
  printf '\nBuilt from %s.\n' "$(git rev-parse --short HEAD)"
}

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
if [[ $DRY_RUN == 1 ]]; then
  release_notes
  exit 0
fi

# The release tags HEAD, so HEAD has to exist on GitHub.
git fetch -q origin
git branch -r --contains HEAD | grep -q . || { echo "HEAD is not pushed to origin" >&2; exit 1; }

AUTH=(-H "Authorization: Bearer ${GITHUB_RELEASE_TOKEN}" -H "Accept: application/vnd.github+json")
NOTES="$(release_notes)"
BODY="$(VERSION="$VERSION" NOTES="$NOTES" python3 -c 'import json, os, subprocess
head = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
v = "v" + os.environ["VERSION"]
print(json.dumps({"tag_name": v, "target_commitish": head, "name": v, "body": os.environ["NOTES"], "make_latest": "true"}))')"
RELEASE="$(curl -fsS "${AUTH[@]}" -H "Content-Type: application/json" -X POST "https://api.github.com/repos/${REPO}/releases" -d "$BODY")"
ID="$(printf '%s' "$RELEASE" | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')"
# The x4pro updater asks for the literal asset name firmware.bin
# (CROSSPOINT_RELEASE_ASSET in FirmwareBoardTag.h).
curl -fsS "${AUTH[@]}" -H "Content-Type: application/octet-stream" --data-binary @"$BIN" \
  "https://uploads.github.com/repos/${REPO}/releases/${ID}/assets?name=firmware.bin" >/dev/null
echo "published v${VERSION}: https://github.com/${REPO}/releases/tag/v${VERSION}"
