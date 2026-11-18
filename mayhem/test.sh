#!/usr/bin/env bash
#
# mayhem/test.sh — behavioral oracle for the RSDKv3 engine's asset loaders.
# Runs the pre-built, unsanitized mayhem/selftest.cc driver (build/bin/rsdk_selftest) over the committed
# seed files; each loader must parse them into exactly the values asserted here (pixel checksums computed
# independently in Python from the generator's known pixels, names/counts/ids read from the fixtures).
# exit 0 = pass. A patch that makes a loader reject, skip or no-op its input changes these values and fails.
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


BIN="$SRC/build/bin/rsdk_selftest"
[ -x "$BIN" ] || { echo "missing $BIN (mayhem/build.sh must build it)" >&2; emit_ctrf "rsdk-selftest" 0 1 0; exit 1; }

passed=0; failed=0

# check <label> <kind> <file> <expected line>...   -- every expected line must appear verbatim in the output
check() {
  local label="$1" kind="$2" file="$3"; shift 3
  local out; out=$("$BIN" "$kind" "$SRC/$file" 2>&1) || true
  local bad=0 want
  for want in "$@"; do
    if ! grep -Fxq -- "$want" <<<"$out"; then
      echo "FAIL $label: expected '$want'" >&2; bad=1
    fi
  done
  if [ "$bad" -eq 0 ]; then passed=$((passed+1)); echo "ok   $label"; else failed=$((failed+1)); echo "$out" | head -20 >&2; fi
}

T=mayhem
check gif-8x8            gif $T/rsdk_gif/testsuite/sheet8x8.gif            width=8 height=8 pixsum=3120 rc=0
check gif-8x8-interlaced gif $T/rsdk_gif/testsuite/sheet8x8_interlaced.gif width=8 height=8 pixsum=3120 rc=0
check gif-16x16          gif $T/rsdk_gif/testsuite/sheet16x16.gif          width=16 height=16 pixsum=49344 rc=0
check sheet-gfx          sheet $T/rsdk_sheet/testsuite/sheet.gfx           width=8 height=4 pixsum=5024 rc=0
check sheet-bmp          sheet $T/rsdk_sheet/testsuite/sheet.bmp           width=8 height=4 pixsum=6320 rc=0

check anim-player        anim $T/rsdk_anim/testsuite/player.ani files=1 anims=2 frames=3 hitboxes=2 \
  anim0=Idle,2,4,0,1 anim1=Run,1,6,0,0 frame0=0,0,16,24,-8,-12,0 frame1=16,0,16,24,-8,-12,1 frame2=32,0,18,24,-9,-12,1 rc=0
check anim-minimal       anim $T/rsdk_anim/testsuite/minimal.ani files=1 anims=0 frames=0 rc=0

check script-aliases     script $T/rsdk_script/testsuite/aliases.txt code=15 jumps=0 funcs=0 codesum=4461 mobile=1 rc=0
check script-object1     script $T/rsdk_script/testsuite/object1.txt code=44 jumps=2 funcs=0 codesum=25821 rc=0
check script-object2     script $T/rsdk_script/testsuite/object2.txt funcs=1 jumps=8 rc=0

check bytecode-packed    bytecode $T/rsdk_bytecode/testsuite/global_packed.bin code=9 jumps=3 w0=1 w1=5 w2=9 w3=12 w4=3 w5=7 w6=0 w7=4096 w8=2 sub_draw=5 rc=0
check bytecode-bytes     bytecode $T/rsdk_bytecode/testsuite/global_bytes.bin code=7 jumps=3 w0=1 w3=12 w6=2 sub_draw=5 rc=0

check gameconfig-full    gameconfig $T/rsdk_gameconfig/testsuite/config1.bin title="Retro-Engine Test" vars=2 var0=Options.Soundtrack,1 var1=Game.Lives,3 \
  stages0=1 stage0_0=Title,1,TITLE,0 stages1=2 stage1_0=R1,1,"Round 1",0 stage1_1=R1,2,"Round 1b",1 stages2=1 stage2_0=Special,1,"Special 1",0 stages3=0 players=2 rc=0
check gameconfig-minimal gameconfig $T/rsdk_gameconfig/testsuite/config2.bin title=Minimal vars=0 stages0=0 stages1=0 rc=0

check stage-act          stage $T/rsdk_stage/testsuite/act1.bin "title=ROUND 1 - ZONE" xsize=2 ysize=2 tiles=1,2,0,0 rc=0
check stage-backgrounds  stage $T/rsdk_stage/testsuite/backgrounds.bin layer1=0,2 hparallax=1,2048 rc=0
check stage-tiles-gif    stage $T/rsdk_stage/testsuite/tiles.gif tilesum=585920 rc=0
check stage-tiles-gfx    stage $T/rsdk_stage/testsuite/tiles.gfx tilesum=65 rc=0

check text-font          text $T/rsdk_text/testsuite/font.fnt char0=65,0,0,8,8,0,8 char1=66,8,0,8,8,0,8 char2=67,16,0,8,8,0,8 rc=0
check text-ascii         text $T/rsdk_text/testsuite/menu_ascii.txt rows=4 row0=83.84.65.82.84.32.71.65.77.69. row1=79.80.84.73.79.78.83. row2=81.85.73.84. rc=0
check text-mapped        text $T/rsdk_text/testsuite/menu_mapped.txt rows=3 row0=33.34.35. row1=35.34.33. rc=0
check text-utf16         text $T/rsdk_text/testsuite/menu_utf16.txt rows=3 row0=72.69.76.76.79. row1=87.79.82.76.68. rc=0

check ini-settings       ini $T/rsdk_ini/testsuite/settings.ini items=8 item=Game/language=0 item=Window/fullscreen=false \
  item=Audio/SFXVolume=80 screenWidth_rc=1 screenWidth=424 vsync_rc=1 vsync=1 rc=0
check ini-mod            ini $T/rsdk_ini/testsuite/mod.ini items=5 "item=/Name=My Mod" item=/Active=y screenWidth_rc=0 rc=0

check datapack-bytecode  datapack $T/rsdk_datapack/testsuite/data1.rsdk "title=Packed Game" vars=1 bytecode=1 stages0=1 other=missing rc=0
check datapack-files     datapack $T/rsdk_datapack/testsuite/data2.rsdk "title=Packed Game" bytecode=0 "other=hello world" rc=0

check gamexml-full       gamexml $T/rsdk_gamexml/testsuite/game1.xml "window=Modded Game" vars=2 var0=Mod.Flag,5 var1=Mod.Other,-2 objs=1 obj0=ModObj,Mod/Obj.txt,1 \
  players=1 player0=SONIC stages0=1 stage0_0=Title,1,Title rc=0
check gamexml-minimal    gamexml $T/rsdk_gamexml/testsuite/game2.xml window=X vars=1 var0=A,1 objs=0 rc=0

emit_ctrf "rsdk-selftest" "$passed" "$failed" 0
