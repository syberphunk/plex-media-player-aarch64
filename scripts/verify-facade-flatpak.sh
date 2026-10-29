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
command -v curl >/dev/null || { echo "curl not found" >&2; exit 1; }
command -v xdotool >/dev/null || { echo "xdotool not found (apt install xdotool)" >&2; exit 1; }

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
if [ "$ARCH" != "$(flatpak --default-arch)" ]; then
  EXTRA_ENV+=(--env=PMP_STARTUP_DEADLINE="$RUN_SECONDS")
fi
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
POINTER=$OUT/pointer.txt
GL_STATE="$HOME/.var/app/$APP_ID/data/plexmediaplayer/gl-state"
rm -f "$GL_STATE"

echo "==> running headless for ${RUN_SECONDS}s, screen grab at ${GRAB_AT}s"
(
  sleep "$GRAB_AT"
  ffmpeg -loglevel error -y -f x11grab -video_size "${WIDTH}x${HEIGHT}" -i "$DISPLAY" \
    -frames:v 1 -pix_fmt rgb24 -f rawvideo "$SHOT" </dev/null
) &
GRAB_PID=$!

(
  trap '[ -s "$POINTER" ] || echo "pointer driver-stopped-early" > "$POINTER"' EXIT

  deadline=$((SECONDS + GRAB_AT - 4))
  window=
  while [ "$SECONDS" -lt "$deadline" ]; do
    window=$(xdotool search --onlyvisible --name 'Plex Media Player' 2>/dev/null | tail -1 || true)
    [ -n "$window" ] || window=$(xdotool search --name 'Plex Media Player' 2>/dev/null | tail -1 || true)
    [ -n "$window" ] || window=$(xdotool search --class plexmediaplayer 2>/dev/null | tail -1 || true)
    [ -n "$window" ] && break
    sleep 1
  done

  if [ -z "$window" ]; then
    echo "pointer no-window" > "$POINTER"
    exit 0
  fi

  moves=0
  wheels=0
  while [ "$SECONDS" -lt "$deadline" ]; do
    x=$(( (RANDOM % (WIDTH - 40)) + 20 ))
    y=$(( (RANDOM % (HEIGHT - 40)) + 20 ))
    if xdotool mousemove --window "$window" "$x" "$y" 2>/dev/null; then
      moves=$((moves + 1))
    fi
    if [ $((moves % 11)) -eq 0 ]; then
      xdotool click 4 2>/dev/null && wheels=$((wheels + 1))
      xdotool click 5 2>/dev/null && wheels=$((wheels + 1))
    fi
    sleep 0.2
  done

  echo "pointer delivered window=$window moves=$moves wheels=$wheels" > "$POINTER"
) &
POINTER_PID=$!

PROBE=$OUT/probe.txt
(
  port=
  deadline=$((SECONDS + GRAB_AT))
  while [ "$SECONDS" -lt "$deadline" ]; do
    for cand in 32433 32434 32435; do
      if curl -s -m 2 "http://127.0.0.1:$cand/identity" 2>/dev/null | grep -q MediaContainer; then
        port=$cand
        break
      fi
    done
    [ -n "$port" ] && break
    sleep 2
  done

  if [ -z "$port" ]; then
    echo "loopback none" > "$PROBE"
    exit 0
  fi
  echo "loopback served port=$port" > "$PROBE"

  lan=$(ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1)
  [ -n "$lan" ] || lan=$(hostname -I 2>/dev/null | awk '{print $1}')
  if [ -z "$lan" ]; then
    echo "offbox skipped" >> "$PROBE"
    exit 0
  fi
  code=$(curl -s -m 5 -o /dev/null -w '%{http_code}' "http://$lan:$port/identity" 2>/dev/null || true)
  echo "offbox $code addr=$lan" >> "$PROBE"
) &
PROBE_PID=$!

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
wait "$PROBE_PID" 2>/dev/null || true
wait "$POINTER_PID" 2>/dev/null || true
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
  'Accepted websocket' \
  'af-remove: item label' \
  'is served by a connector' \
  ; do
  n=$(grep -cF "$marker" "$OUT/run.log" || true)
  printf '    %-45s %s\n' "$marker" "$n"
done

echo
echo "==> synthetic pointer input"
if [ -f "$POINTER" ]; then
  sed 's/^/    /' "$POINTER"
else
  echo "    the pointer driver produced nothing"
fi

echo
echo "==> recorded process state"
if [ -f "$GL_STATE" ]; then
  sed 's/^/    /' "$GL_STATE"
else
  echo "    no gl-state file was written"
fi

echo
echo "==> embedded server exposure"
if [ -f "$PROBE" ]; then
  sed 's/^/    /' "$PROBE"
else
  echo "    the probe produced nothing"
fi

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

if [ "$rc" -eq 124 ]; then
  echo "    pass  the app ran until the timeout without exiting"
else
  echo "    FAIL  the app exited on its own with $rc before the timeout"
  fail=1
fi

crash=$(grep -c '^crash=' "$GL_STATE" 2>/dev/null || true)
if [ "${crash:-0}" -eq 0 ]; then
  echo "    pass  the app recorded no crash"
else
  echo "    FAIL  the app recorded a crash: $(grep '^crash=' "$GL_STATE" | tr '\n' ' ')"
  echo "          the backtrace is in ./facade-verify-$ARCH.log"
  fail=1
fi

pointer=$(awk '/^pointer/{print $2}' "$POINTER" 2>/dev/null || true)
if [ "$pointer" = delivered ]; then
  echo "    pass  the app survived synthetic pointer input"
else
  echo "    FAIL  no pointer input was delivered to the window ($pointer)"
  echo "          the input paths in the app were not exercised by this run"
  fail=1
fi

if grep -qF 'Could not create QtWebEngine' "$OUT/run.log"; then
  echo "    FAIL  QtWebEngine did not start"
  fail=1
else
  echo "    pass  QtWebEngine started"
fi

jserr=$(grep -F '] JS: ' "$OUT/run.log" | grep -cE 'Uncaught|TypeError|ReferenceError|Maximum call stack' || true)
if [ "$jserr" -eq 0 ]; then
  echo "    pass  the client logged no uncaught exception"
else
  echo "    FAIL  the client logged $jserr uncaught exception(s)"
  grep -F '] JS: ' "$OUT/run.log" \
    | grep -E 'Uncaught|TypeError|ReferenceError|Maximum call stack' \
    | head -10 | sed 's/^/          /'
  fail=1
fi

loopback=none
offbox=skipped
if [ -f "$PROBE" ]; then
  loopback=$(awk '/^loopback/{print $2}' "$PROBE")
  offbox=$(awk '/^offbox/{print $2}' "$PROBE")
  [ -n "$offbox" ] || offbox=skipped
fi

if [ "$loopback" = served ]; then
  echo "    pass  the embedded server answered the local client"
else
  echo "    FAIL  the embedded server never answered on loopback"
  fail=1
fi

case $offbox in
  skipped) echo "    pass  no routable address to probe from off-box" ;;
  000|404) echo "    pass  the embedded server is not exposed off-box ($offbox)" ;;
  *)       echo "    FAIL  the embedded server answered off-box with $offbox"
           fail=1 ;;
esac

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
