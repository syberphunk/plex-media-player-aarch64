#!/usr/bin/env bash
set -uo pipefail
TAG=${1:?usage: playtest.sh <tag> [seconds] [VAR=VALUE ...]}
SECS=${2:-90}
shift 2 || true

HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${BUILD:-$(cd "$HERE/../../.." && pwd)/build}
PAGE=${PAGE:-$HERE/drivepage/index.html}
CLIP=${CLIP:-$HERE/testclip.mp4}
MODE=${MODE:-desktop}
RUNDIR=$HERE/run-$TAG

[ -x "$BUILD/src/plexmediaplayer" ] || { echo "no binary at $BUILD/src/plexmediaplayer" >&2; exit 1; }

rm -rf "$RUNDIR"; mkdir -p "$RUNDIR/data"

reap() {
  pkill -f "$BUILD/src/plexmediaplayer" 2>/dev/null
  for _ in 1 2; do sleep 1; pkill -9 -f "$BUILD/src/plexmediaplayer" 2>/dev/null; done
  return 0
}
reap
trap 'reap; kill "${XPID:-}" 2>/dev/null; exit' EXIT INT TERM

for n in $(seq 130 160); do
  [ -e "/tmp/.X$n-lock" ] || { DISP=$n; break; }
done
Xvfb ":$DISP" -screen 0 1280x720x24 +extension GLX +render -noreset \
  >"$RUNDIR/xvfb.log" 2>&1 &
XPID=$!
sleep 2

export XDG_DATA_HOME="$RUNDIR/data"
export DISPLAY=":$DISP"
for kv in "$@"; do export "${kv?}"; done

CONF="$XDG_DATA_HOME/plexmediaplayer/plexmediaplayer.conf"
timeout 20 "$BUILD/src/plexmediaplayer" --help >/dev/null 2>&1
mkdir -p "$(dirname "$CONF")"
python3 -I - "$CONF" "$PAGE" "$CLIP" "$MODE" <<'PY'
import json, os, sys
path, page, clip, mode = sys.argv[1:5]
conf = json.load(open(path)) if os.path.exists(path) else {}
conf.setdefault("version", 4)
s = conf.setdefault("sections", {})
url = "file://" + page + "?clip=file://" + clip
s.setdefault("path", {})["startupurl_desktop"] = url
s["path"]["startupurl_tv"] = url
s.setdefault("main", {})["webMode"] = mode
s.setdefault("system", {})["logLevel"] = "debug"
json.dump(conf, open(path, "w"), indent=2)
print("startupurl ->", url, "mode ->", mode)
PY

timeout "$SECS" "$BUILD/src/plexmediaplayer" >"$RUNDIR/stdout.log" 2>&1 &
PMPPID=$!
for t in 20 40 60; do
  [ "$t" -gt "$SECS" ] && break
  sleep 20
  ffmpeg -loglevel error -y -f x11grab -video_size 1280x720 -i ":$DISP" \
    -frames:v 1 "$RUNDIR/shot-$t.png" 2>>"$RUNDIR/shots.log" || true
done
wait $PMPPID
echo "==> exit $? (124 = ran to the timeout, the good case)"

LOG=$RUNDIR/data/plexmediaplayer/logs/plexmediaplayer.log
echo "==> log: $LOG"
echo
echo "=== DRIVE transcript ==="
sed -n 's/.*DRIVE /DRIVE /p' "$LOG"
