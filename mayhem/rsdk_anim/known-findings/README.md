Upstream bug that the harness's `MIN_STARTUP_INPUT` check (`mayhem/targets.hpp`) skips for short inputs.
Not a seed: this directory is outside every Mayhemfile testsuite.

`crash-sheet-name-length-past-eof` (five `0xFF` bytes): `LoadAnimationFile` reads a sheet-name length byte and
then that many bytes into `char strBuf[0x21]` (`RSDKv3/Animation.cpp:33`). Nothing checks the length against
the buffer or the file size, and `FileRead` has no end-of-file check: ASan stack-buffer-overflow in `FileRead`
at `RSDKv3/Reader.cpp:458:25`. libFuzzer's 1-4 byte startup inputs hit the same reads.

    /mayhem/rsdk_anim-standalone mayhem/rsdk_anim/known-findings/crash-sheet-name-length-past-eof
