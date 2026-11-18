Upstream bug that the harness's `MIN_STARTUP_INPUT` check (`mayhem/targets.hpp`) skips for short inputs.
Not a seed: this directory is outside every Mayhemfile testsuite.

`crash-background-scan-past-eof` (selector `1` = Backgrounds.bin, then five `0xFF` bytes): `LoadStageBackground`
reads the line-scroll table until a `0xFF` byte (`RSDKv3/Scene.cpp:798`), with layer counts taken from the
header. `FileRead` has no end-of-file check, so it reads past EOF until it indexes `fileBuffer[8192]`: UBSan
index-out-of-bounds at `RSDKv3/Reader.cpp:458:27`. The same happens for short .gif (`Scene.cpp:1069`) and .gfx
(`Scene.cpp:1120`) tile files, and libFuzzer's short startup inputs hit these loops.

    /mayhem/rsdk_stage-standalone mayhem/rsdk_stage/known-findings/crash-background-scan-past-eof
