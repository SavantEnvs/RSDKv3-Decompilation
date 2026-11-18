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

# 1) Build the engine (RSDKv3/*.cpp except main.cpp, plus fcaseopen.c / theoraplay.c) with the flags
#    upstream's CMakeLists.txt passes for Linux. tinyxml2 is the apt libtinyxml2 (upstream's
#    dependencies/all/tinyxml2 is an empty submodule). The sanitized objects also get
#    -fsanitize=fuzzer-no-link so libFuzzer sees coverage of the engine, not just the harness.
B="$SRC/build"
mkdir -p "$B/san" "$B/norm" "$B/bin"
PKGS="sdl2 ogg vorbis vorbisfile theora theoradec glew tinyxml2"
CF="$(pkg-config --cflags $PKGS) -I$SRC/RSDKv3 -I$SRC/dependencies/all/theoraplay"
CF="$CF -DRETRO_USE_MOD_LOADER=1 -DRETRO_USING_OPENGL=1 -DRETRO_USE_SDL2=1 -DFORCE_CASE_INSENSITIVE=0 -DRETRO_USE_ORIGINAL_CODE=0 -DRSDK_AUTOBUILD=0"
LIBS="$(pkg-config --libs $PKGS) -lm -lpthread"
# UBSan relaxations (the only ones; ASan and every other UBSan check stay on, halting):
#   Script.cpp: -fno-sanitize=signed-integer-overflow -- the script compiler parses decimal literals as
#   `value = value * 10 + digit` into an int, so any long digit string overflows by design.
relax() { case "$1" in Script.cpp) echo "-fno-sanitize=signed-integer-overflow" ;; esac; }

SRCS=$(cd "$SRC" && ls RSDKv3/*.cpp | grep -v '/main\.cpp$')
compile() { # <mode> <src>
  local mode="$1" f="$2" o="$B/$1/$(basename "${2%.*}").o"
  case "$mode" in
    san)  FLAGS="-O1 $SANITIZER_FLAGS $(relax "$(basename "$f")") -fsanitize=fuzzer-no-link $DEBUG_FLAGS" ;;
    norm) FLAGS="-O2 $COVERAGE_FLAGS" ;;
  esac
  case "$f" in
    *.cpp) "$CXX" -std=c++17 -w $FLAGS $CF -c "$SRC/$f" -o "$o" ;;
    *.c)   "$CC" -w $FLAGS $CF -c "$SRC/$f" -o "$o" ;;
  esac
}
export -f compile relax; export B SRC CC CXX SANITIZER_FLAGS DEBUG_FLAGS COVERAGE_FLAGS CF
ALL="$SRCS RSDKv3/fcaseopen.c dependencies/all/theoraplay/theoraplay.c"
for mode in san norm; do
  printf '%s\n' $ALL | xargs -P "$MAYHEM_JOBS" -I{} bash -c 'compile '"$mode"' {}'
done
OBJS_SAN=$(ls "$B"/san/*.o)
OBJS_NORM=$(ls "$B"/norm/*.o)

# 2) One harness per target, linked twice: libFuzzer, and the run-once standalone reproducer.
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/standalone_main.obj"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$B/lsan_off.obj"
TARGETS="rsdk_gif rsdk_sheet rsdk_anim rsdk_script rsdk_bytecode rsdk_gameconfig rsdk_stage rsdk_text rsdk_ini rsdk_datapack rsdk_gamexml"
link_target() {
  local t="$1"
  "$CXX" -std=c++17 -O1 -w $SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS $CF -c "$SRC/mayhem/$t/$t.cc" -o "$B/$t.obj"
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$B/$t.obj" $OBJS_SAN "$B/lsan_off.obj" $LIBS -o "$SRC/$t"
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS "$B/$t.obj" "$B/standalone_main.obj" $OBJS_SAN "$B/lsan_off.obj" $LIBS -o "$SRC/$t-standalone"
}
export -f link_target; export OBJS_SAN LIBS LIB_FUZZING_ENGINE
printf '%s\n' $TARGETS | xargs -P "$MAYHEM_JOBS" -I{} bash -c 'link_target {}'

# 3) The oracle driver, built with the project's normal flags (no sanitizers).
"$CXX" -std=c++17 -O2 -w $COVERAGE_FLAGS $CF -c "$SRC/mayhem/selftest.cc" -o "$B/norm/selftest.obj"
"$CXX" $COVERAGE_FLAGS "$B/norm/selftest.obj" $OBJS_NORM $LIBS -o "$B/bin/rsdk_selftest"
