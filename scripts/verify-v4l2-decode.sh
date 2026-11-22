#!/usr/bin/env bash
# Checks that the V4L2 hardware decoder produces the same picture as software
# decoding, for 8 bit and 10 bit HEVC. Run it on the device with the decoder,
# using an ffmpeg built from the FFmpeg source and patches in
# packaging/flatpak/patches (the app itself ships only the libraries).
#
#   [MAKE_CLIPS_WITH=ffmpeg-with-libx265] scripts/verify-v4l2-decode.sh [path-to-ffmpeg]
#
# The test clips are made with libx265. When the ffmpeg under test has no
# encoder, name another ffmpeg that does in MAKE_CLIPS_WITH.
#
# A 10 bit clip used to fail here: the decoder switched its output to a
# vendor-compressed layout that FFmpeg could not read, so no frame came out.

set -euo pipefail

FFMPEG=${1:-ffmpeg}
ENCODER=${MAKE_CLIPS_WITH:-$FFMPEG}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

command -v "$FFMPEG" >/dev/null || { echo "ffmpeg not found: $FFMPEG" >&2; exit 1; }
decoders=$("$FFMPEG" -hide_banner -decoders 2>/dev/null || true)
encoders=$("$ENCODER" -hide_banner -encoders 2>/dev/null || true)
case $decoders in *hevc_v4l2m2m*) ;; *) echo "this ffmpeg has no hevc_v4l2m2m decoder" >&2; exit 1 ;; esac
case $encoders in *libx265*) ;; *) echo "$ENCODER has no libx265 encoder, needed to make the test clips" >&2; exit 1 ;; esac

fail=0
for depth in 8 10; do
  if [ "$depth" = 10 ]; then pix=yuv420p10le; else pix=yuv420p; fi
  clip=$WORK/clip$depth.mkv

  "$ENCODER" -hide_banner -loglevel error -f lavfi -i testsrc2=size=1920x1080:rate=24 -t 4 \
    -c:v libx265 -pix_fmt "$pix" -x265-params log-level=error "$clip"

  # Compare in the format the hardware decoder hands out for this depth.
  out=p010le
  [ "$depth" = 8 ] && out=nv12

  if ! "$FFMPEG" -hide_banner -loglevel error -c:v hevc_v4l2m2m -i "$clip" -pix_fmt $out \
        -f framemd5 "$WORK/hw$depth.md5" 2>"$WORK/hw$depth.err"; then
    echo "FAIL  $depth bit: the hardware decoder did not decode the clip"
    head -5 "$WORK/hw$depth.err" | sed 's/^/        /'
    fail=1
    continue
  fi
  if ! "$FFMPEG" -hide_banner -loglevel error -c:v hevc -i "$clip" -pix_fmt $out \
        -f framemd5 "$WORK/sw$depth.md5"; then
    echo "FAIL  $depth bit: software decoding of the test clip failed"
    fail=1
    continue
  fi

  # framemd5 lines are "stream, dts, pts, duration, size, md5"; keep the md5.
  awk -F, '!/^#/ {print $6}' "$WORK/hw$depth.md5" > "$WORK/hw$depth.sums"
  awk -F, '!/^#/ {print $6}' "$WORK/sw$depth.md5" > "$WORK/sw$depth.sums"
  hw_frames=$(grep -c . "$WORK/hw$depth.sums" || true)
  sw_frames=$(grep -c . "$WORK/sw$depth.sums" || true)
  if [ "$sw_frames" -gt 0 ] && cmp -s "$WORK/hw$depth.sums" "$WORK/sw$depth.sums"; then
    echo "ok    $depth bit: $sw_frames frames identical to software decoding"
  else
    echo "FAIL  $depth bit: hardware gave $hw_frames frames, software $sw_frames, and they differ or are missing"
    fail=1
  fi
done

exit $fail
