#!/usr/bin/env bash

set -euo pipefail

REPO_DIR=${1:?usage: verify-facade-flatpak.sh <repo-dir> [arch] [seconds]}
ARCH=${2:-$(flatpak --default-arch)}
RUN_SECONDS=${3:-45}

APP_ID=tv.plex.PlexMediaPlayer
REMOTE=pmp-verify-$ARCH
OUT=$(mktemp -d)

command -v Xvfb >/dev/null || { echo "Xvfb not found (apt install xvfb)" >&2; exit 1; }

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
      Xvfb ":$n" -screen 0 1280x720x24 -listen tcp -ac >/dev/null 2>&1 &
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

echo "==> installing $APP_ID/$ARCH from $REPO_DIR"
flatpak --user remote-add --no-gpg-verify --no-enumerate --if-not-exists "$REMOTE" "$REPO_DIR"
flatpak --user install -y --arch="$ARCH" --reinstall "$REMOTE" "$APP_ID" >/dev/null

rm -rf "$HOME/.var/app/$APP_ID/config/plex" "$HOME/.var/app/$APP_ID/data/plex"

EXTRA_ENV=()
if [ "$ARCH" != "$(flatpak --default-arch)" ]; then
  echo "==> $ARCH != host $(flatpak --default-arch): emulated run," \
       "forcing single-process Chromium"
  EXTRA_ENV+=(--env=QTWEBENGINE_CHROMIUM_FLAGS="--single-process --no-sandbox --disable-gpu-sandbox")
fi

echo "==> running headless for ${RUN_SECONDS}s"
set +e
timeout --signal=TERM "$RUN_SECONDS" \
  flatpak run --arch="$ARCH" --env=DISPLAY="$DISPLAY" \
    --env=LIBGL_ALWAYS_SOFTWARE=1 --env=QT_X11_NO_MITSHM=1 \
    "${EXTRA_ENV[@]}" "$APP_ID" \
    --terminal --desktop --windowed --no-updates \
  > "$OUT/run.log" 2>&1
rc=$?
set -e
echo "    exited with $rc (124 = ran until the timeout, which is the good case)"

APP_LOG="$HOME/.var/app/$APP_ID/cache/plex/logs/Plex Media Player.log"
[ -f "$APP_LOG" ] && cat "$APP_LOG" >> "$OUT/run.log"

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
grep -nE 'ERROR|FATAL|WARN|Uncaught|undefined is not' "$OUT/run.log" | head -30 || echo "    none"

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
  echo "          scripts/patch-web-client.py must re-sign index.html"
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
  echo "VERIFICATION FAILED -- do not publish this build"
  exit 1
fi

echo
echo "VERIFICATION PASSED"
