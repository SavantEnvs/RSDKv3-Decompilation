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

// A file shorter than the fixed header makes the loader read past EOF into the stale file buffer (Reader.cpp
// FileRead has no EOF check); those inputs are rejected so every run starts from a file with a header at least.
inline int run_gif(const uint8_t *d, size_t n)
{
    if (n < 14)
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
    if (n < 1)
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
    if (n < 8)
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
    if (n < 1)
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
