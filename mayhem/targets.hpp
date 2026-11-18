// Shared by the fuzz harnesses and by the oracle (selftest.cpp). Each run_* function is the whole
// per-input recipe for one fuzz target: put the bytes where the engine itself looks for that file,
// reset the engine state the previous input could have left behind, call the engine's loader.
#pragma once
#include "RetroEngine.hpp"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Copy of the file-static decoder state in Sprite.cpp, so it can be zeroed between inputs.
struct MhGifDecoder {
    int depth, clearCode, eofCode, runningCode, runningBits, prevCode, currentCode, maxCodePlusOne, stackPtr, shiftState, fileState, position,
        bufferSize;
    uint shiftData;
    uint pixelCount;
    byte buffer[256];
    byte stack[4096];
    byte suffix[4096];
    uint prefix[4096];
};
extern struct MhGifDecoder gifDecoder;

namespace mh
{

inline int &scratch_fd()
{
    static int fd = -1;
    return fd;
}

// The engine opens "Data/..." relative to the cwd, so every input runs inside a scratch dir; the cwd is put
// back afterwards because the fuzz drivers open their own input files by relative path.
inline void scratch_init()
{
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp)
        tmp = "/tmp";
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/rsdk.XXXXXX", tmp);
    if (!mkdtemp(dir))
        abort();
    scratch_fd() = open(dir, O_RDONLY | O_DIRECTORY);
    if (scratch_fd() < 0)
        abort();
    static const char *const dirs[] = { "Data",          "Data/Sprites", "Data/Animations", "Data/Scripts", "Data/Scripts/ByteCode",
                                        "Data/Game",     "Data/Stages",  "Data/Stages/S",   "Data/Fonts" };
    for (const char *d : dirs) {
        char path[600];
        snprintf(path, sizeof(path), "%s/%s", dir, d);
        mkdir(path, 0755);
    }
    // A valid 8x8 sprite sheet that animation files can name ("Player.gif", as the rsdk_anim seeds do), so
    // LoadAnimationFile's AddGraphicsFile call loads a real sheet instead of failing to open it. Written once
    // and never modified, so it is read-only state shared by every input.
    static const unsigned char sheet[] = { 0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x08, 0x00, 0x08, 0x00, 0x91, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x05, 0x0b, 0x11, 0x0a, 0x16, 0x22, 0x0f, 0x21, 0x33, 0x2c, 0x00, 0x00,
                                           0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x00, 0x02, 0x0f, 0x44, 0x34, 0x86, 0x97, 0x0c,
                                           0xa8, 0x5a, 0x83, 0x27, 0x46, 0xe7, 0x28, 0x9d, 0xb3, 0x00, 0x00, 0x3b };
    char path[600];
    snprintf(path, sizeof(path), "%s/Data/Sprites/Player.gif", dir);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(sheet, 1, sizeof(sheet), f) != sizeof(sheet) || fclose(f) != 0)
        abort();
}

struct Enter {
    int orig;
    Enter()
    {
        orig = open(".", O_RDONLY | O_DIRECTORY);
        if (orig < 0 || fchdir(scratch_fd()) != 0)
            abort();
    }
    ~Enter()
    {
        if (fchdir(orig) != 0)
            abort();
        close(orig);
    }
};

inline bool put(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    if (size && fwrite(data, 1, size, f) != size) {
        fclose(f);
        return false;
    }
    return fclose(f) == 0;
}

inline void reset_io()
{
    CloseFile();
    memset(fileBuffer, 0, sizeof(fileBuffer));
    fileSize = vFileSize = readPos = readSize = bufferPosition = virtualFileOffset = 0;
    eStringPosA = eStringPosB = eStringNo = eNybbleSwap = 0;
    Engine.usingDataFile = Engine.usingDataFile_Config = Engine.usingDataFileStore = false;
    Engine.forceFolder                                                              = false;
    Engine.usingBytecode                                                            = false;
    Engine.bytecodeMode                                                             = BYTECODE_MOBILE;
    Engine.gameMode                                                                 = ENGINE_MAINGAME;
    modList.clear();
    activeMod = -1;
}

inline void reset_gfx()
{
    memset(&gifDecoder, 0, sizeof(gifDecoder));
    ClearGraphicsData();
}

// Documented exception (accepted by the maintainers in PR review) to "no filter before the loader", for
// run_gif, run_bytecode, run_gameconfig, run_anim, run_datapack, run_sheet and run_stage. Each of these
// loaders crashes on libFuzzer's first inputs: the empty input it always runs first, "\n", and the 1-4 byte
// mutants it grows from them when it starts without a corpus (as Mayhem's start-of-run check does). The
// target then never reaches iteration 5, and Mayhem fails the run ("libFuzzer target failed to fuzz for 5
// iterations"). All of them read through FileRead (Reader.cpp:458), which has no end-of-file check, so a
// file shorter than its header runs on into fileBuffer[8192] or a caller's fixed buffer:
//   LoadGIFFile       Sprite.cpp:355       `while (fileBuffer != ',') FileRead(&fileBuffer, 1);`  (empty file)
//   LoadBytecode      Script.cpp:1935      block-header read after the 4-byte size                ("\n")
//   LoadGameConfig    RetroEngine.cpp:1036 `gameWindowText[fileBuffer] = 0;` with an unchecked length byte
//   LoadAnimationFile      Animation.cpp:33  sheet-name length byte read into strBuf[0x21], unchecked
//   ParseVirtualFileSystem Reader.cpp:289    directory-name length byte read past EOF into stringBuffer
//   LoadGFXFile            Sprite.cpp:419    `while (true) { FileRead(&buf[0], 1); if (buf[0] == 0xFF)`  (sheet)
//   LoadStageBackground    Scene.cpp:798     same 0xFF scan; also LoadStageGIFFile Scene.cpp:1069 and
//                                            LoadStageGFXFile Scene.cpp:1120                             (stage)
// rsdk_sheet and rsdk_stage spend byte 0 on a selector, so they require MIN_STARTUP_INPUT payload bytes after it.
// These are upstream bugs, not harness bugs; mayhem/<target>/known-findings/ holds a reproducer for each
// that is at least MIN_STARTUP_INPUT bytes long, so the shipped binaries still reach them.
// MIN_STARTUP_INPUT is the smallest size that passes: with 5, gif, bytecode and gameconfig completed 40/40
// empty-corpus libFuzzer runs of 5 and of 20 iterations; with 4 they failed 9-14 of 40 runs of 20 iterations.
// With 5, anim, datapack, sheet and stage also complete 40/40 of 5 and of 20 iterations (without it, 29, 19, 2 and 2 of 40 runs of 5 failed).
// Every other target has no size check.
static const size_t MIN_STARTUP_INPUT = 5;

inline int run_gif(const uint8_t *d, size_t n)
{
    if (n < MIN_STARTUP_INPUT) // see MIN_STARTUP_INPUT: Sprite.cpp:355 via FileRead Reader.cpp:458
        return -1;
    reset_io();
    reset_gfx();
    if (!put("Data/Sprites/in.gif", d, n))
        abort();
    return LoadGIFFile("Data/Sprites/in.gif", 0) ? 0 : -1;
}

// byte 0: 0 = .gfx sheet, 1 = .bmp sheet
inline int run_sheet(const uint8_t *d, size_t n)
{
    if (n < 1 + MIN_STARTUP_INPUT) // see MIN_STARTUP_INPUT: Sprite.cpp:419 via FileRead Reader.cpp:458
        return -1;
    reset_io();
    reset_gfx();
    if (d[0] == 0) {
        if (!put("Data/Sprites/in.gfx", d + 1, n - 1))
            abort();
        return LoadGFXFile("Data/Sprites/in.gfx", 0) ? 0 : -1;
    }
    if (d[0] == 1) {
        if (!put("Data/Sprites/in.bmp", d + 1, n - 1))
            abort();
        return LoadBMPFile("Data/Sprites/in.bmp", 0) ? 0 : -1;
    }
    return -1;
}

inline int run_anim(const uint8_t *d, size_t n)
{
    if (n < MIN_STARTUP_INPUT) // see MIN_STARTUP_INPUT: Animation.cpp:33 via FileRead Reader.cpp:458
        return -1;
    reset_io();
    reset_gfx();
    ClearAnimationData();
    if (!put("Data/Animations/in.ani", d, n))
        abort();
    return AddAnimationFile("in.ani") ? 0 : -1;
}

inline int run_script(const uint8_t *d, size_t n)
{
    reset_io();
    reset_gfx();
    ClearScriptData();
    if (!put("Data/Scripts/in.txt", d, n))
        abort();
    char name[] = "in.txt";
    ParseScriptFile(name, 1);
    return Engine.gameMode == ENGINE_SCRIPTERROR ? -1 : 0;
}

inline int run_bytecode(const uint8_t *d, size_t n)
{
    if (n < MIN_STARTUP_INPUT) // see MIN_STARTUP_INPUT: Script.cpp:1935 via FileRead Reader.cpp:458
        return -1;
    reset_io();
    reset_gfx();
    ClearScriptData();
    if (!put("Data/Scripts/ByteCode/GlobalCode.bin", d, n))
        abort();
    LoadBytecode(4, 1);
    return 0;
}

inline int run_gameconfig(const uint8_t *d, size_t n)
{
    if (n < MIN_STARTUP_INPUT) // see MIN_STARTUP_INPUT: RetroEngine.cpp:1036 via FileRead Reader.cpp:458
        return -1;
    reset_io();
    if (!put("Data/Game/GameConfig.bin", d, n))
        abort();
    return Engine.LoadGameConfig("Data/Game/GameConfig.bin") ? 0 : -1;
}

inline void stage_select()
{
    activeStageList   = STAGELIST_PRESENTATION;
    stageListPosition = 0;
    StrCopy(stageList[activeStageList][0].folder, "S");
    StrCopy(stageList[activeStageList][0].id, "1");
}

// byte 0: 0 = Act1.bin, 1 = Backgrounds.bin, 2 = 16x16Tiles.gif, 3 = 16x16Tiles.gfx
inline int run_stage(const uint8_t *d, size_t n)
{
    if (n < 1 + MIN_STARTUP_INPUT) // see MIN_STARTUP_INPUT: Scene.cpp:798 via FileRead Reader.cpp:458
        return -1;
    reset_io();
    reset_gfx();
    stage_select();
    switch (d[0]) {
        case 0:
            if (!put("Data/Stages/S/Act1.bin", d + 1, n - 1))
                abort();
            LoadActLayout();
            return 0;
        case 1:
            if (!put("Data/Stages/S/Backgrounds.bin", d + 1, n - 1))
                abort();
            LoadStageBackground();
            return 0;
        case 2:
            if (!put("Data/Stages/S/16x16Tiles.gif", d + 1, n - 1))
                abort();
            LoadStageGIFFile(0);
            return 0;
        case 3:
            if (!put("Data/Stages/S/16x16Tiles.gfx", d + 1, n - 1))
                abort();
            LoadStageGFXFile(0);
            return 0;
    }
    return -1;
}

// byte 0: 0 = font file, 1 = text file, 2 = text file mapped through the font
inline int run_text(const uint8_t *d, size_t n)
{
    if (n < 1)
        return -1;
    reset_io();
    memset(fontCharacterList, 0, sizeof(fontCharacterList));
    if (d[0] == 2) {
        for (int i = 0; i < 95; ++i) fontCharacterList[i].id = 32 + i;
    }
    memset(&gameMenu[0], 0, sizeof(gameMenu[0]));
    if (!put("Data/Game/in.dat", d + 1, n - 1))
        abort();
    switch (d[0]) {
        case 0: LoadFontFile("Data/Game/in.dat"); return 0;
        case 1: LoadTextFile(&gameMenu[0], "Data/Game/in.dat", 0); return 0;
        case 2: LoadTextFile(&gameMenu[0], "Data/Game/in.dat", 1); return 0;
    }
    return -1;
}

inline int run_ini(const uint8_t *d, size_t n)
{
    reset_io();
    if (!put("in.ini", d, n))
        abort();
    IniParser ini("in.ini", false);
    char str[0x100];
    int i;
    float f;
    bool b;
    ini.GetString("Game", "language", str);
    ini.GetInteger("Window", "screenWidth", &i);
    ini.GetFloat("Window", "scale", &f);
    ini.GetBool("Window", "fullscreen", &b);
    ini.Write("out.ini", false);
    return 0;
}

// The fuzz target passes parseConfig=false: GameConfig.bin parsing has its own target (rsdk_gameconfig), and
// leaving it on makes nearly every mutated payload byte hit the same LoadGameConfig overflow.
inline int run_datapack(const uint8_t *d, size_t n, bool parseConfig = true)
{
    if (n < MIN_STARTUP_INPUT) // see MIN_STARTUP_INPUT: Reader.cpp:289 via FileRead Reader.cpp:458
        return -1;
    reset_io();
    reset_gfx();
    ClearScriptData();
    if (!put("Data.rsdk", d, n))
        abort();
    if (!CheckRSDKFile("Data.rsdk"))
        return -1;
    FileInfo info;
    static const char *const names[] = { "Data/Game/GameConfig.bin", "Data/Scripts/ByteCode/GlobalCode.bin" };
    for (const char *nm : names) {
        if (LoadFile(nm, &info)) {
            byte buf[0x400];
            int len = info.vFileSize < (int)sizeof(buf) ? info.vFileSize : (int)sizeof(buf);
            if (len > 0)
                FileRead(buf, len);
            CloseFile();
        }
    }
    if (parseConfig)
        Engine.LoadGameConfig("Data/Game/GameConfig.bin");
    Engine.usingDataFile = false;
    return 0;
}

inline int run_gamexml(const uint8_t *d, size_t n)
{
    reset_io();
    if (!put("in.xml", d, n))
        abort();
    ModInfo mod;
    mod.active                                  = true;
    mod.useScripts                              = false;
    mod.disableFocusPause                       = 0;
    mod.redirectSave                            = false;
    mod.disableSaveIniOverride                  = false;
    mod.fileMap["data/game/game.xml"] = "in.xml";
    modList.push_back(mod);
    globalVariablesCount = 0;
    playerCount          = 0;
    memset(stageListCount, 0, sizeof(stageListCount));
    Engine.LoadXMLWindowText();
    Engine.LoadXMLVariables();
    Engine.LoadXMLPalettes();
    Engine.LoadXMLObjects();
    Engine.LoadXMLPlayers(NULL);
    Engine.LoadXMLStages(NULL, 0);
    modList.clear();
    return 0;
}

} // namespace mh
