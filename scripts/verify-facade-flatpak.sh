#!/usr/bin/env bash

set -euo pipefail

REPO_DIR=${1:?usage: verify-facade-flatpak.sh <repo-dir> [arch] [seconds]}
ARCH=${2:-$(flatpak --default-arch)}
RUN_SECONDS=${3:-45}

APP_ID=io.github.syberphunk.PlexMediaPlayer
REMOTE=plex-media-player-aarch64-verify-$ARCH
OUT=$(mktemp -d)
PUBKEY=$(cd "$(dirname "$0")/.." && pwd)/packaging/flatpak/signing-key.asc

command -v Xvfb >/dev/null || { echo "Xvfb not found (apt install xvfb)" >&2; exit 1; }
command -v ffmpeg >/dev/null || { echo "ffmpeg not found (apt install ffmpeg)" >&2; exit 1; }
command -v python3 >/dev/null || { echo "python3 not found" >&2; exit 1; }

WIDTH=1280
HEIGHT=720

if [ ! -w /tmp/.X11-unix ] || [ ! -k /tmp/.X11-unix ]; then
  echo "/tmp/.X11-unix must be writable and mode 1777 for the X server to start." >&2
  echo "On WSL:  sudo mount -o remount,rw /tmp/.X11-unix && sudo chmod 1777 /tmp/.X11-unix" >&2
  exit 1
fi

start_xvfb() {
  local n
  for n in $(seq 99 120); do
    [ -e "/tmp/.X$n-lock" ] && continue
    LIBGL_ALWAYS_SOFTWARE=1 __EGL_VENDOR_LIBRARY_FILENAMES=/dev/null \
      Xvfb ":$n" -screen 0 "${WIDTH}x${HEIGHT}x24" -listen tcp -ac >/dev/null 2>&1 &
    XVFB_PID=$!
    for _ in $(seq 1 40); do
      kill -0 "$XVFB_PID" 2>/dev/null || break
      if (exec 3<>/dev/tcp/127.0.0.1/$((6000 + n))) 2>/dev/null; then
        DISPLAY="127.0.0.1:$n"
        export DISPLAY
        echo "==> Xvfb on $DISPLAY (pid $XVFB_PID)"
        return 0
      fi
      sleep 0.25
    done
    kill "$XVFB_PID" 2>/dev/null || true
  done
  echo "could not start Xvfb on any display" >&2
  exit 1
}

XVFB_PID=
trap 'rm -rf "$OUT"; [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null' EXIT
start_xvfb

[ -d "$REPO_DIR" ] || { echo "$REPO_DIR is not a directory" >&2; exit 1; }
REPO_URL=file://$(cd "$REPO_DIR" && pwd)

echo "==> installing $APP_ID/$ARCH from $REPO_URL"
GPG_REMOTE_ARGS=(--no-gpg-verify)
if [ -n "${FP_GPG_HOMEDIR:-}" ]; then
  [ -f "$PUBKEY" ] || { echo "$PUBKEY not found" >&2; exit 1; }
  command -v gpg >/dev/null || { echo "gpg not found" >&2; exit 1; }
  gpg --dearmor < "$PUBKEY" > "$OUT/signing-key.gpg"
  GPG_REMOTE_ARGS=(--gpg-import="$OUT/signing-key.gpg")
  echo "==> the remote verifies against $PUBKEY"
fi

flatpak --user remote-delete --force "$REMOTE" 2>/dev/null || true
flatpak --user remote-add "${GPG_REMOTE_ARGS[@]}" --no-enumerate "$REMOTE" "$REPO_URL"
flatpak --user install -y --arch="$ARCH" --reinstall "$REMOTE" "$APP_ID" >/dev/null

INSTALLED_COMMIT=$(flatpak --user info --show-commit --arch="$ARCH" "$APP_ID")
REPO_COMMIT=$(flatpak --user remote-info --show-commit --arch="$ARCH" "$REMOTE" "$APP_ID")
if [ "$INSTALLED_COMMIT" != "$REPO_COMMIT" ]; then
  echo "installed commit $INSTALLED_COMMIT does not match $REPO_DIR ($REPO_COMMIT)" >&2
  exit 1
fi
echo "==> verifying commit $REPO_COMMIT"

flatpak kill "$APP_ID" 2>/dev/null || true

rm -rf "$HOME/.var/app/$APP_ID/config/plex.tv" \
       "$HOME/.var/app/$APP_ID/data/plexmediaplayer" \
       "$HOME/.var/app/$APP_ID/cache/plexmediaplayer" \
       "$HOME/.var/app/$APP_ID/cache/mesa_shader_cache" \
       "$HOME/.var/app/$APP_ID/cache/qtshadercache-"*

EXTRA_ENV=()
if [ -n "${QTWEBENGINE_CHROMIUM_FLAGS:-}" ]; then
  echo "==> Chromium flags from the environment: $QTWEBENGINE_CHROMIUM_FLAGS"
  EXTRA_ENV+=(--env=QTWEBENGINE_CHROMIUM_FLAGS="$QTWEBENGINE_CHROMIUM_FLAGS")
elif [ "$ARCH" != "$(flatpak --default-arch)" ]; then
  echo "==> $ARCH != host $(flatpak --default-arch): emulated run, disabling the Chromium sandbox"
  EXTRA_ENV+=(--env=QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox")
fi

GRAB_AT=$((RUN_SECONDS - 8))
[ "$GRAB_AT" -lt 4 ] && GRAB_AT=$((RUN_SECONDS / 2))
SHOT=$OUT/shot.rgb

echo "==> running headless for ${RUN_SECONDS}s, screen grab at ${GRAB_AT}s"
(
  sleep "$GRAB_AT"
  ffmpeg -loglevel error -y -f x11grab -video_size "${WIDTH}x${HEIGHT}" -i "$DISPLAY" \
    -frames:v 1 -pix_fmt rgb24 -f rawvideo "$SHOT" </dev/null
) &
GRAB_PID=$!

set +e
timeout --signal=TERM "$RUN_SECONDS" \
  flatpak run --arch="$ARCH" --socket=x11 --nosocket=wayland --env=DISPLAY="$DISPLAY" \
    --env=LIBGL_ALWAYS_SOFTWARE=1 --env=QT_X11_NO_MITSHM=1 \
    "${EXTRA_ENV[@]}" "$APP_ID" \
    --terminal --desktop --windowed \
  > "$OUT/run.log" 2>&1
rc=$?
set -e
echo "    exited with $rc (124 = ran until the timeout, which is the good case)"

wait "$GRAB_PID" 2>/dev/null || true
flatpak kill "$APP_ID" 2>/dev/null || true

cat > "$OUT/pixels.py" <<'PY'
import sys

path, width, height = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(path, "rb") as fp:
    raw = fp.read()

expected = width * height * 3
if len(raw) < expected:
    sys.exit("frame is %d bytes, expected %d" % (len(raw), expected))

lit = 0
colours = set()
for i in range(0, expected, 3):
    r, g, b = raw[i], raw[i + 1], raw[i + 2]
    if r > 24 or g > 24 or b > 24:
        lit += 1
    colours.add((r >> 3, g >> 3, b >> 3))

pixels = width * height
print("lit=%.4f colours=%d" % (lit / pixels, len(colours)))
if lit / pixels < 0.02:
    sys.exit("only %.2f%% of the frame is non-black: the window painted nothing"
             % (100.0 * lit / pixels))
if len(colours) < 16:
    sys.exit("the frame holds %d distinct colours: no client UI was drawn"
             % len(colours))
PY

APP_LOG="$HOME/.var/app/$APP_ID/data/plexmediaplayer/logs/plexmediaplayer.log"
if [ -f "$APP_LOG" ] && ! grep -qE '\[ (DEBUG|INFO|WARN|ERROR) +\]' "$OUT/run.log"; then
  cat "$APP_LOG" >> "$OUT/run.log"
fi

echo
echo "==> bridge calls observed"
if grep -oE '\b(system|settings|player|storage|input|display|server|update|user)\.[a-zA-Z]+' "$OUT/run.log" \
   | sort | uniq -c | sort -rn | head -40 | grep .; then
  :
else
  echo "    NONE -- the client did not reach the bridge"
fi

echo
echo "==> handshake markers"
for marker in \
  'Web client reports ready' \
  'system.describe' \
  'Ambiguous overloads' \
  'Unable to connect to the embedded server' \
  'unknown keys' \
  'Could not create QtWebEngine' \
  'JS: ' \
  'not mono-threaded' \
  ; do
  n=$(grep -cF "$marker" "$OUT/run.log" || true)
  printf '    %-45s %s\n' "$marker" "$n"
done

echo
echo "==> errors and warnings"
grep -nE 'ERROR|FATAL|WARN|Uncaught|undefined is not' "$OUT/run.log" \
  | grep -vF '] JS: ' | head -30 || echo "    none"

cp "$OUT/run.log" "./facade-verify-$ARCH.log"
echo
echo "Full log: ./facade-verify-$ARCH.log"

echo
echo "==> verdict"
fail=0

ready=$(grep -cF 'Web client reports ready' "$OUT/run.log" || true)
if [ "$ready" -ge 1 ]; then
  echo "    pass  the web client completed the host handshake"
else
  echo "    FAIL  the web client never reported ready"
  fail=1
fi

describe=$(grep -cF 'system.describe' "$OUT/run.log" || true)
if [ "$describe" -ge 1 ]; then
  echo "    pass  the client called into the bridge"
else
  echo "    FAIL  no bridge call reached the host"
  fail=1
fi

blocked=$(grep -cE "integrity.*has been blocked|Failed to find a valid digest" "$OUT/run.log" || true)
if [ "$blocked" -eq 0 ]; then
  echo "    pass  no subresource integrity failure"
else
  echo "    FAIL  the bundle was blocked by subresource integrity ($blocked)"
  echo "          the staged client no longer matches the hashes in index.html"
  fail=1
fi

if [ -s "$SHOT" ]; then
  ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 \
    -video_size "${WIDTH}x${HEIGHT}" -i "$SHOT" "./facade-verify-$ARCH.png" </dev/null
  if pixels=$(python3 -I "$OUT/pixels.py" "$SHOT" "$WIDTH" "$HEIGHT" 2>&1); then
    echo "    pass  the window rendered a client UI ($pixels)"
  else
    echo "    FAIL  $pixels"
    echo "          see ./facade-verify-$ARCH.png"
    fail=1
  fi
else
  echo "    FAIL  no screen grab was captured"
  fail=1
fi

if grep -qF 'Could not create QtWebEngine' "$OUT/run.log"; then
  echo "    FAIL  QtWebEngine did not start"
  fail=1
else
  echo "    pass  QtWebEngine started"
fi

if [ "$fail" -ne 0 ]; then
  echo
  echo "==> last host lines"
  grep -vF '] JS: ' "$OUT/run.log" | tail -60
  echo
  echo "VERIFICATION FAILED -- do not publish this build"
  exit 1
fi

echo
echo "VERIFICATION PASSED"
