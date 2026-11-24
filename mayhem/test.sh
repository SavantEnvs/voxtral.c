#!/usr/bin/env bash
#
# mayhem/test.sh — RUN this repo's OWN functional test suite (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# Known-answer tests over the WAV / mel / safetensors / tokenizer APIs (mayhem/selftest.c,
# built by build.sh). Every expected test name must print PASS; a missing one counts as failed.
RUNNER="$SRC/mayhem/build-fuzz/selftest"
[ -x "$RUNNER" ] || { echo "missing $RUNNER (run mayhem/build.sh first)" >&2; emit_ctrf selftest 0 1; exit 1; }
EXPECTED="wav_sample_count wav_sample_values mel_frame_count mel_value_range mel_checksum
mel_stream_checksum mel_stream_discard wav_ffmpeg_pipe_size wav_stereo_resample wav_reject_8bit
wav_reject_magic st_open st_shapes st_f32 st_bf16_to_f32 st_f16_to_f32 st_bf16_raw st_reject_oob
tok_load tok_decode_b64 tok_special_unicode tok_decode_seq"
out="$("$RUNNER" "$SRC/samples/test_speech.wav" 2>/dev/null)"; rc=$?
echo "$out"
passed=0; failed=0
for t in $EXPECTED; do
  if grep -qx "PASS $t" <<<"$out"; then passed=$((passed + 1)); else failed=$((failed + 1)); echo "not passed: $t"; fi
done
[ "$rc" -eq 0 ] || { echo "selftest exited $rc"; [ "$failed" -gt 0 ] || failed=1; }
emit_ctrf selftest "$passed" "$failed" 0
