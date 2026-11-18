Upstream bug that the harness's `MIN_STARTUP_INPUT` check (`mayhem/targets.hpp`) skips for inputs under 5 bytes.
Not a seed: this directory is outside every Mayhemfile testsuite.

`crash-comma-scan-past-eof` (`GIF89`): `LoadGIFFile` scans for the image separator with
`while (fileBuffer != ',') FileRead(&fileBuffer, 1);` (`RSDKv3/Sprite.cpp:355`). `FileRead` has no end-of-file
check, so in a file with no `,` it keeps reading until it indexes `fileBuffer[8192]`: UBSan
index-out-of-bounds at `RSDKv3/Reader.cpp:458:27`. libFuzzer's empty startup input hits the same loop.

    /mayhem/rsdk_gif-standalone mayhem/rsdk_gif/known-findings/crash-comma-scan-past-eof
