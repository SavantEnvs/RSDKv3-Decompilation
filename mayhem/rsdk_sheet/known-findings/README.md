Upstream bug that the harness's `MIN_STARTUP_INPUT` check (`mayhem/targets.hpp`) skips for short inputs.
Not a seed: this directory is outside every Mayhemfile testsuite.

`crash-gfx-scan-past-eof` (selector `0` = .gfx, then five zero bytes): `LoadGFXFile` scans pixel runs until a
`0xFF` byte (`RSDKv3/Sprite.cpp:419`). `FileRead` has no end-of-file check, so in a file with no `0xFF` it keeps
reading until it indexes `fileBuffer[8192]`: UBSan index-out-of-bounds at `RSDKv3/Reader.cpp:458:27`.
libFuzzer's short startup inputs hit the same loop.

    /mayhem/rsdk_sheet-standalone mayhem/rsdk_sheet/known-findings/crash-gfx-scan-past-eof
