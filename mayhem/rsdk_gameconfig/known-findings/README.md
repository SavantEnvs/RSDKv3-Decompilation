Upstream bug that the harness's `MIN_STARTUP_INPUT` check (`mayhem/targets.hpp`) skips for inputs under 5 bytes.
Not a seed: this directory is outside every Mayhemfile testsuite.

`crash-gamewindowtext-length-byte` (`41 41 41 41 41`): `LoadGameConfig` takes the first byte (0x41 = 65) as the
window-title length unchecked, and `gameWindowText[fileBuffer] = 0;` indexes past `char gameWindowText[0x40]`:
UBSan index-out-of-bounds at `RSDKv3/RetroEngine.cpp:1036`. The title read itself goes through `FileRead`
(`RSDKv3/Reader.cpp:458`), which has no end-of-file check. libFuzzer's empty and 1-4 byte startup inputs hit
the same code.

    /mayhem/rsdk_gameconfig-standalone mayhem/rsdk_gameconfig/known-findings/crash-gamewindowtext-length-byte
