Upstream bug that the harness's `MIN_STARTUP_INPUT` check (`mayhem/targets.hpp`) skips for short inputs.
Not a seed: this directory is outside every Mayhemfile testsuite.

`crash-directory-read-past-eof` (`RSDKv`): `CheckRSDKFile` → `LoadFile` → `ParseVirtualFileSystem` reads the
directory table past the end of a 5-byte pack. `FileRead` has no end-of-file check, so a directory-name length
byte from past EOF overflows `stringBuffer` (`RSDKv3/Reader.cpp:289`): ASan stack-buffer-overflow in `FileRead`
at `RSDKv3/Reader.cpp:458:25`. libFuzzer's 1-4 byte startup inputs hit the same reads.

    /mayhem/rsdk_datapack-standalone mayhem/rsdk_datapack/known-findings/crash-directory-read-past-eof
