// Oracle driver: runs one input through the same recipe a fuzz target uses, then prints what the engine
// parsed out of it (key=value lines) so mayhem/test.sh can compare against known answers.
#include "targets.hpp"

static unsigned long long pixsum(const byte *p, int n)
{
    unsigned long long s = 0;
    for (int i = 0; i < n; ++i) s = (s + (unsigned long long)(i + 1) * p[i]) % 1000000007ULL;
    return s;
}

static void dump_surface(int id)
{
    printf("width=%d\nheight=%d\n", gfxSurface[id].width, gfxSurface[id].height);
    printf("pixsum=%llu\n", pixsum(&graphicData[gfxSurface[id].dataPosition], gfxSurface[id].width * gfxSurface[id].height));
}

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    FILE *f = fopen(argv[2], "rb");
    if (!f)
        return 2;
    static uint8_t buf[1 << 20];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    mh::scratch_init();
    mh::Enter in;

    const char *k = argv[1];
    int rc        = -2;
    if (!strcmp(k, "gif")) {
        rc = mh::run_gif(buf, n);
        dump_surface(0);
    }
    else if (!strcmp(k, "sheet")) {
        rc = mh::run_sheet(buf, n);
        dump_surface(0);
    }
    else if (!strcmp(k, "anim")) {
        rc = mh::run_anim(buf, n);
        printf("files=%d\nanims=%d\nframes=%d\nhitboxes=%d\n", animationFileCount, animationCount, animFrameCount, hitboxCount);
        for (int a = 0; a < animationCount; ++a)
            printf("anim%d=%s,%d,%d,%d,%d\n", a, animationList[a].name, animationList[a].frameCount, animationList[a].speed,
                   animationList[a].loopPoint, animationList[a].rotationStyle);
        for (int i = 0; i < animFrameCount; ++i)
            printf("frame%d=%d,%d,%d,%d,%d,%d,%d\n", i, animFrames[i].sprX, animFrames[i].sprY, animFrames[i].width, animFrames[i].height,
                   animFrames[i].pivotX, animFrames[i].pivotY, animFrames[i].hitboxID);
    }
    else if (!strcmp(k, "script")) {
        rc = mh::run_script(buf, n);
        printf("code=%d\njumps=%d\nfuncs=%d\n", scriptCodePos, jumpTablePos, scriptFunctionCount);
        printf("codesum=%llu\n", pixsum((const byte *)scriptCode, scriptCodePos * 4));
        printf("sub_main=%d\n", objectScriptList[1].subMain.scriptCodePtr);
        printf("mobile=%d\n", (int)objectScriptList[1].mobile);
        if (rc)
            printf("error_line=%d\nerror_text=%s\n", lineID, scriptText);
    }
    else if (!strcmp(k, "bytecode")) {
        rc = mh::run_bytecode(buf, n);
        printf("code=%d\njumps=%d\nfuncs=%d\n", scriptCodePos, jumpTablePos, scriptFunctionCount);
        for (int i = 0; i < 9; ++i) printf("w%d=%d\n", i, scriptCode[i]);
        printf("sub_main=%d,%d\n", objectScriptList[1].subMain.scriptCodePtr, objectScriptList[1].subMain.jumpTablePtr);
        printf("sub_draw=%d\n", objectScriptList[1].subDraw.scriptCodePtr);
    }
    else if (!strcmp(k, "gameconfig")) {
        rc = mh::run_gameconfig(buf, n);
        printf("title=%s\nvars=%d\n", Engine.gameWindowText, globalVariablesCount);
        for (int i = 0; i < globalVariablesCount; ++i) printf("var%d=%s,%d\n", i, globalVariableNames[i], globalVariables[i]);
        for (int l = 0; l < 4; ++l) {
            printf("stages%d=%d\n", l, stageListCount[l]);
            for (int s = 0; s < stageListCount[l]; ++s)
                printf("stage%d_%d=%s,%s,%s,%d\n", l, s, stageList[l][s].folder, stageList[l][s].id, stageList[l][s].name, (int)stageList[l][s].highlighted);
        }
        printf("players=%d\n", playerCount);
    }
    else if (!strcmp(k, "stage")) {
        rc = mh::run_stage(buf, n);
        if (buf[0] == 0) {
            printf("title=%s\nxsize=%d\nysize=%d\n", titleCardText, stageLayouts[0].xsize, stageLayouts[0].ysize);
            printf("tiles=%d,%d,%d,%d\n", stageLayouts[0].tiles[0], stageLayouts[0].tiles[1], stageLayouts[0].tiles[2], stageLayouts[0].tiles[3]);
        }
        else if (buf[0] == 1) {
            printf("layer1=%d,%d\nlayer2=%d,%d\n", stageLayouts[1].xsize, stageLayouts[1].ysize, stageLayouts[2].xsize, stageLayouts[2].ysize);
            printf("hparallax=%d,%d\n", hParallax.parallaxFactor[0], hParallax.scrollSpeed[0]);
        }
        else {
            printf("tilesum=%llu\n", pixsum(tilesetGFXData, 16 * 16 * 2));
        }
    }
    else if (!strcmp(k, "text")) {
        rc = mh::run_text(buf, n);
        if (buf[0] == 0) {
            for (int i = 0; i < 3; ++i)
                printf("char%d=%d,%d,%d,%d,%d,%d,%d\n", i, fontCharacterList[i].id, fontCharacterList[i].srcX, fontCharacterList[i].srcY,
                       fontCharacterList[i].width, fontCharacterList[i].height, fontCharacterList[i].pivotX, fontCharacterList[i].xAdvance);
        }
        else {
            printf("rows=%d\n", gameMenu[0].rowCount);
            for (int r = 0; r < gameMenu[0].rowCount; ++r) {
                printf("row%d=", r);
                for (int c = 0; c < gameMenu[0].entrySize[r]; ++c) printf("%d.", gameMenu[0].textData[gameMenu[0].entryStart[r] + c]);
                printf("\n");
            }
        }
    }
    else if (!strcmp(k, "ini")) {
        rc = mh::run_ini(buf, n);
        IniParser ini("in.ini", false);
        printf("items=%d\n", (int)ini.items.size());
        for (auto &it : ini.items) printf("item=%s/%s=%s\n", it.section, it.key, it.value);
        int v = -1;
        printf("screenWidth_rc=%d\n", ini.GetInteger("Window", "screenWidth", &v));
        printf("screenWidth=%d\n", v);
        bool b = false;
        printf("vsync_rc=%d\n", ini.GetBool("Window", "vsync", &b));
        printf("vsync=%d\n", (int)b);
    }
    else if (!strcmp(k, "datapack")) {
        rc = mh::run_datapack(buf, n);
        printf("title=%s\nvars=%d\n", Engine.gameWindowText, globalVariablesCount);
        printf("bytecode=%d\n", (int)Engine.usingBytecode);
        printf("stages0=%d\n", stageListCount[0]);
        {
            FileInfo info;
            Engine.usingDataFile = true;
            if (LoadFile("Data/Game/Other.bin", &info)) {
                char s[32] = { 0 };
                FileRead(s, info.vFileSize < 31 ? info.vFileSize : 31);
                CloseFile();
                printf("other=%s\n", s);
            }
            else
                printf("other=missing\n");
        }
    }
    else if (!strcmp(k, "gamexml")) {
        rc = mh::run_gamexml(buf, n);
        printf("window=%s\nvars=%d\n", Engine.gameWindowText, globalVariablesCount);
        for (int i = 0; i < globalVariablesCount; ++i) printf("var%d=%s,%d\n", i, globalVariableNames[i], globalVariables[i]);
        printf("objs=%d\n", modObjCount);
        for (int i = 0; i < modObjCount; ++i) printf("obj%d=%s,%s,%d\n", i, modTypeNames[i], modScriptPaths[i], modScriptFlags[i]);
        printf("players=%d\n", playerCount);
        for (int i = 0; i < playerCount; ++i) printf("player%d=%s\n", i, playerNames[i]);
        printf("stages0=%d\n", stageListCount[0]);
        for (int i = 0; i < stageListCount[0]; ++i) printf("stage0_%d=%s,%s,%s\n", i, stageList[0][i].folder, stageList[0][i].id, stageList[0][i].name);
    }
    printf("rc=%d\n", rc);
    return 0;
}
