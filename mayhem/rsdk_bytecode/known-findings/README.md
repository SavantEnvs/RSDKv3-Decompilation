Upstream bug that the harness's `MIN_STARTUP_INPUT` check (`mayhem/targets.hpp`) skips for inputs under 5 bytes.
Not a seed: this directory is outside every Mayhemfile testsuite.

`crash-header-read-past-eof` (`0a 0a 0a 0a 0a`): `LoadBytecode` reads a 4-byte script-code size
(0x0a0a0a0a) and then keeps reading block headers (`RSDKv3/Script.cpp:1935`) through `FileRead`, which has no
end-of-file check, until it indexes `fileBuffer[8192]`: UBSan index-out-of-bounds at `RSDKv3/Reader.cpp:458:27`.
libFuzzer's `"\n"` startup input hits the same read.

    /mayhem/rsdk_bytecode-standalone mayhem/rsdk_bytecode/known-findings/crash-header-read-past-eof
