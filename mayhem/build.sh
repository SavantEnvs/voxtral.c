#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# voxtral.c is a plain Makefile project whose CLI needs BLAS and the 8.9 GB model. The
# model-free parsers (WAV + mel, safetensors, tokenizer) are three self-contained C files;
# build them with the sanitizer + debug flags and link them into each harness.
LIBSRC=(voxtral_audio.c voxtral_safetensors.c voxtral_tokenizer.c)
OBJ="$SRC/mayhem/build-fuzz"
mkdir -p "$OBJ"
# voxtral_audio.c's read_u32() does `p[3] << 24` on an int: every chunk size with the top
# bit set (e.g. ffmpeg's 0xFFFFFFFF streaming data size, the case upstream 80f2fa9 handles)
# is a signed-shift UB report. Relax only shift-base on that one file so the halting UBSan
# does not stop every such WAV before the chunk walker; everything else stays halting.
AUDIO_RELAX=""
case "$SANITIZER_FLAGS" in *undefined*) AUDIO_RELAX="-fno-sanitize=shift-base" ;; esac
# Two object sets: the libFuzzer build carries SanitizerCoverage (-fsanitize=fuzzer-no-link,
# so the parsers' edges are visible to the fuzzer); the standalone reproducer is built without
# it, so it also links when SANITIZER_FLAGS is empty.
COV_FLAGS=""
case "$LIB_FUZZING_ENGINE" in *-fsanitize=fuzzer*) COV_FLAGS="-fsanitize=fuzzer-no-link" ;; esac
build_objs() {  # <dir> <extra compile flags>
  local d="$1" x="$2"; mkdir -p "$d"
  for f in "${LIBSRC[@]}"; do
    local extra=""; [ "$f" = voxtral_audio.c ] && extra="$AUDIO_RELAX"
    $CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 $x $extra -I"$SRC" -c "$SRC/$f" -o "$d/${f%.c}.o"
  done
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.c" -o "$d/lsan_off.o"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/harnesses/vox_globals.c" -o "$d/vox_globals.o"
  for h in "${HARNESSES[@]}"; do
    $CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 $x -I"$SRC" -c "$SRC/mayhem/harnesses/$h.c" -o "$d/$h.o"
  done
}
HARNESSES=(fuzz_wav fuzz_safetensors fuzz_tokenizer fuzz_mel_stream)
build_objs "$OBJ/fuzz" "$COV_FLAGS"
build_objs "$OBJ/standalone" ""
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/standalone/standalone_main.o"
LIBOBJS=(voxtral_audio.o voxtral_safetensors.o voxtral_tokenizer.o lsan_off.o vox_globals.o)
for h in "${HARNESSES[@]}"; do
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OBJ/fuzz/$h.o" "${LIBOBJS[@]/#/$OBJ/fuzz/}" -lm -o "/mayhem/$h"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS "$OBJ/standalone/standalone_main.o" "$OBJ/standalone/$h.o" \
      "${LIBOBJS[@]/#/$OBJ/standalone/}" -lm -o "/mayhem/$h-standalone"
done

# Functional oracle (upstream's runtest.sh needs the model + ffmpeg): known-answer tests over
# the same APIs, built with normal flags (no sanitizers) so test.sh only runs it.
$CC -O2 $COVERAGE_FLAGS -I"$SRC" "$SRC/mayhem/selftest.c" "$SRC/mayhem/harnesses/vox_globals.c" "${LIBSRC[@]/#/$SRC/}" -lm -o "$OBJ/selftest"
