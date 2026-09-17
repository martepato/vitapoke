#include "screen_fade.h"

#include <string.h>

#include "constants/graphics.h"
#include "constants/heap.h"

#include "brightness_controller.h"
#include "enums.h"
#include "hardware_window.h"
#include "heap.h"
#include "screen_fade_funcs.h"
#include "sys_task.h"
#include "sys_task_manager.h"
#include "system.h"

#define LOCAL_TASK_PRIORIITY 1024

enum ScreenFadeOrder {
    ORDER_SIMULTANEOUS,
    ORDER_MAIN_FIRST,
    ORDER_SUB_FIRST
};

typedef struct EnableHBlankTemplate {
    ScreenFadeHBlanks *hblanks;
    void *data;
    Callback callback;
    enum DSScreen screen;
} EnableHBlankTemplate;

typedef struct DisableHBlankTemplate {
    ScreenFadeHBlanks *hblanks;
    enum DSScreen screen;
} DisableHBlankTemplate;

typedef struct ScreenFadeParams {
    enum ScreenFadeOrder order;
    BOOL activeMain;
    BOOL activeSub;
    BOOL existsMain;
    BOOL existsSub;
} ScreenFadeParams;

typedef struct ScreenFadeManager {
    ScreenFadeParams screen;
    ScreenFade mainScreenFade;
    ScreenFade subScreenFade;
    ScreenFadeHBlanks hblanks;
    HardwareWindowSettings hwSettings;
    u16 active;
    u8 dummy_14E;
    u8 dummy_14F;
    u16 savedColor;
} ScreenFadeManager;

static void ResetScreenFadeManager(ScreenFadeManager *manager);
static void ZeroScreenFadeManager(ScreenFadeManager *manager);
static void ClearHBlanks(ScreenFadeHBlanks *hblanks);
static void RunHBlankCallbacks(void *data);
static BOOL TryScreenFade(ScreenFadeParams *screen, ScreenFade *fadeMain, ScreenFade *fadeSub);
static void TryScreenFadeFunc(BOOL *running, ScreenFade *fade);
static BOOL CallScreenFadeFunc(ScreenFade *fade);
static void SetupScreenFadeParams(enum FadeMode mode, ScreenFadeParams *params);
static void InitScreenFade(ScreenFade *fade, enum FadeType type, int steps, int framesPerStep, enum FadeState state, void *data, enum DSScreen screen, HardwareWindowSettings *hwSettings, ScreenFadeHBlanks *hblanks, enum HeapID heapID, u16 color);
static void InitScreenFadeParams(ScreenFadeParams *params, enum ScreenFadeOrder order, BOOL fadeMain, BOOL fadeSub);
static void EnableScreenHBlank(ScreenFadeHBlanks *hblanks, void *data, Callback callback, enum DSScreen screen);
static void DisableScreenHBlank(ScreenFadeHBlanks *hblanks, enum DSScreen screen);
static u16 GetFadeColor(ScreenFadeManager *manager, u16 color);
static u16 GetSavedFadeColor(const ScreenFadeManager *manager);
static void ResetWindowScreenFade(ScreenFade *fade);
static void RequestResetScreenMasterBrightness(ScreenFade *fade);

static void Task_EnableScreenHBlank(SysTask *task, void *data);
static void Task_DisableScreenHBlank(SysTask *task, void *data);
static void Task_ResetScreenMasterBrightness(SysTask *task, void *data);

static void DummyHBlankCallback(void *data);

/* Only brightness types have completed the SoulSilver assembly audit. */
static const ScreenFadeFunc sScreenFadeFuncs[10] = { [0]=sub_0200F85C, [1]=sub_0200F878, [5]=sub_0200F948, [8]=sub_0200F9D8, [9]=sub_0200FA14 };

extern unsigned char ssdata_unk_0200FA24__021D0EF4[];
#define sScreenFadeManager (*(ScreenFadeManager *)(void *)ssdata_unk_0200FA24__021D0EF4)
_Static_assert(sizeof(ScreenFadeManager)==0x154,"SoulSilver fade manager ABI");
_Static_assert(offsetof(ScreenFadeManager,active)==0x14c,"SoulSilver fade active ABI");
_Static_assert(offsetof(ScreenFadeManager,savedColor)==0x150,"SoulSilver fade saved-color ABI");
_Static_assert(sizeof(ScreenFade)==0x30,"SoulSilver fade work ABI");
_Static_assert(offsetof(ScreenFade,data)==0x14,"SoulSilver fade work data ABI");
_Static_assert(sizeof(ScreenFadeHBlanks)==0x18,"SoulSilver HBlank ABI");
_Static_assert(sizeof(HardwareWindowSettings)==0xc0,"SoulSilver hardware-window ABI");

extern int SSFade_CopyWindowRows(const ScreenFade *fade,unsigned char rows[192]);
/* A renderer hook, using the same buffer the original HBlank callback reads. */
int SSNativeFadeWindowRows(unsigned screen,unsigned char rows[192])
{
    if(screen>=2 || !rows)return -1;
    ScreenFadeManager *manager=&sScreenFadeManager;
    if(!manager->active || !manager->hblanks.running[screen])return 0;
    return SSFade_CopyWindowRows(screen?&manager->subScreenFade:&manager->mainScreenFade,rows);
}


void StartScreenFade(enum FadeMode mode, enum FadeType typeMain, enum FadeType typeSub, u16 color, int steps, int framesPerStep, enum HeapID heapID)
{
    GF_ASSERT(steps > 0);
    GF_ASSERT((unsigned)mode < 5);
    /* PSP port: only the brightness fades (and 5, 8) are implemented in this engine.
       The Bag opens with a downward wipe (types 3/3) and asserted here. Until the
       wipe routines are ported, an unimplemented type becomes the brightness fade in
       the same direction (even = out, odd = in), so the game keeps going. */
    {
        extern void VitaNativeMemLog(const char *, ...);
        enum FadeType origMain = typeMain, origSub = typeSub;
        if ((unsigned)typeMain >= 10 || !sScreenFadeFuncs[typeMain]) typeMain = (typeMain & 1) ? FADE_TYPE_BRIGHTNESS_IN : FADE_TYPE_BRIGHTNESS_OUT;
        if ((unsigned)typeSub >= 10 || !sScreenFadeFuncs[typeSub]) typeSub = (typeSub & 1) ? FADE_TYPE_BRIGHTNESS_IN : FADE_TYPE_BRIGHTNESS_OUT;
        if (origMain != typeMain || origSub != typeSub) VitaNativeMemLog("[SS-FADE] wipe type %d/%d not ported: brightness %d/%d used", (int)origMain, (int)origSub, (int)typeMain, (int)typeSub);
    }
    GF_ASSERT((unsigned)typeMain < 10 && (unsigned)typeSub < 10);
    GF_ASSERT(sScreenFadeFuncs[typeMain] && sScreenFadeFuncs[typeSub]);
    GF_ASSERT(framesPerStep > 0);
    GF_ASSERT(sScreenFadeManager.active == FALSE);

    ScreenFadeManager *manager = &sScreenFadeManager;
    ZeroScreenFadeManager(manager);
    SetupScreenFadeParams(mode, &manager->screen);
    ClearHBlanks(&manager->hblanks);

    u16 fadeColor = GetFadeColor(manager, color);
    InitScreenFade(&manager->mainScreenFade, typeMain, steps, framesPerStep, FADE_IDLE, NULL, DS_SCREEN_MAIN, &manager->hwSettings, &manager->hblanks, heapID, fadeColor);
    InitScreenFade(&manager->subScreenFade, typeSub, steps, framesPerStep, FADE_IDLE, NULL, DS_SCREEN_SUB, &manager->hwSettings, &manager->hblanks, heapID, fadeColor);

    manager->active = TRUE;
    TryScreenFadeFunc(&manager->screen.activeMain, &manager->mainScreenFade);
    TryScreenFadeFunc(&manager->screen.activeSub, &manager->subScreenFade);

    if (manager->screen.existsMain) {
        RequestResetScreenMasterBrightness(&manager->mainScreenFade);
        manager->dummy_14E = 1;
    }

    if (manager->screen.existsSub) {
        RequestResetScreenMasterBrightness(&manager->subScreenFade);
        manager->dummy_14F = 1;
    }
}

void ExecScreenFade(void)
{
    ScreenFadeManager *manager = &sScreenFadeManager;
    if (manager->active && TryScreenFade(&manager->screen, &manager->mainScreenFade, &manager->subScreenFade) == TRUE) {
        ResetScreenFadeManager(manager);
    }
}

BOOL IsScreenFadeDone(void)
{
    return !sScreenFadeManager.active;
}

void FinishScreenFade(void)
{
    DisableScreenHBlank(&sScreenFadeManager.hblanks, DS_SCREEN_MAIN);
    DisableScreenHBlank(&sScreenFadeManager.hblanks, DS_SCREEN_SUB);

    if (sScreenFadeManager.screen.activeMain) {
        sScreenFadeManager.mainScreenFade.state = FADE_CLEANUP;
    }

    if (sScreenFadeManager.screen.activeSub) {
        sScreenFadeManager.subScreenFade.state = FADE_CLEANUP;
    }

    TryScreenFadeFunc(&sScreenFadeManager.screen.activeMain, &sScreenFadeManager.mainScreenFade);
    TryScreenFadeFunc(&sScreenFadeManager.screen.activeSub, &sScreenFadeManager.subScreenFade);

    sScreenFadeManager.active = 0;
    sScreenFadeManager.dummy_14E = 0;
    sScreenFadeManager.dummy_14F = 0;

    ZeroScreenFadeManager(&sScreenFadeManager);
}

void ResetVisibleHardwareWindows(enum DSScreen screen)
{
    SetVisibleHardwareWindows(GX_WNDMASK_NONE, screen);
}

void ResetScreenMasterBrightness(enum DSScreen screen)
{
    SetScreenMasterBrightness(screen, 0);
}

void SetScreenColorBrightness(enum DSScreen screen, u16 color)
{
    if (color == COLOR_SAVED) {
        color = sScreenFadeManager.savedColor;
    }

    int brightness;
    if (color == COLOR_WHITE) {
        brightness = BRIGHTNESS_WHITE;
    } else {
        brightness = BRIGHTNESS_BLACK;
    }

    SetScreenMasterBrightness(screen, brightness);
}

void SetColorBrightness(u16 color)
{
    if (color == COLOR_SAVED) {
        color = sScreenFadeManager.savedColor;
    }

    int brightness;
    if (color == COLOR_WHITE) {
        brightness = BRIGHTNESS_WHITE;
    } else {
        brightness = BRIGHTNESS_BLACK;
    }

    SetScreenMasterBrightness(DS_SCREEN_MAIN, brightness);
    SetScreenMasterBrightness(DS_SCREEN_SUB, brightness);
    sScreenFadeManager.savedColor = color;
}

void SetupScreenFadeRegisters(enum DSScreen screen, u16 color)
{
    if (color == COLOR_SAVED) {
        color = sScreenFadeManager.savedColor;
    }

    if (screen == DS_SCREEN_MAIN) {
        GX_LoadBGPltt((void *)&color, 0, sizeof(u16));
    } else {
        GXS_LoadBGPltt((void *)&color, 0, sizeof(u16));
    }

    RequestVisibleHardwareWindows(&sScreenFadeManager.hwSettings, GX_WNDMASK_W0, screen);
    RequestHardwareWindowMaskInsidePlane(&sScreenFadeManager.hwSettings, GX_BLEND_ALL, 0, 0, screen);
    RequestHardwareWindowDimensions(&sScreenFadeManager.hwSettings, 0, 0, 0, 0, 0, screen);
    RequestHardwareWindowMaskOutsidePlane(&sScreenFadeManager.hwSettings, GX_BLEND_PLANEMASK_BD, 0, screen);
}

void SetScreenBackgroundColor(u16 color)
{
    GX_LoadBGPltt((void *)&color, 0, sizeof(u16));
    GXS_LoadBGPltt((void *)&color, 0, sizeof(u16));
}

void SetScreenMasterBrightness(enum DSScreen screen, int brightness)
{
    if (screen == DS_SCREEN_MAIN) {
        GX_SetMasterBrightness(brightness);
    } else {
        GXS_SetMasterBrightness(brightness);
    }
}

static void ResetScreenFadeManager(ScreenFadeManager *manager)
{
    manager->active = FALSE;
    manager->savedColor = GetSavedFadeColor(manager);

    if (manager->screen.existsMain) {
        ResetWindowScreenFade(&manager->mainScreenFade);
        if (manager->mainScreenFade.direction == FADE_IN) {
            sScreenFadeManager.dummy_14E = 0;
        }
    }

    if (manager->screen.existsSub) {
        ResetWindowScreenFade(&manager->subScreenFade);
        if (manager->mainScreenFade.direction == FADE_IN) {
            sScreenFadeManager.dummy_14F = 0;
        }
    }

    ZeroScreenFadeManager(manager);
}

static BOOL TryScreenFade(ScreenFadeParams *screen, ScreenFade *fadeMain, ScreenFade *fadeSub)
{
    switch (screen->order) {
    case ORDER_SIMULTANEOUS:
        TryScreenFadeFunc(&screen->activeMain, fadeMain);
        TryScreenFadeFunc(&screen->activeSub, fadeSub);
        break;

    case ORDER_MAIN_FIRST:
        if (screen->activeMain) {
            TryScreenFadeFunc(&screen->activeMain, fadeMain);
        } else {
            TryScreenFadeFunc(&screen->activeSub, fadeSub);
        }
        break;

    case ORDER_SUB_FIRST:
        if (screen->activeSub) {
            TryScreenFadeFunc(&screen->activeSub, fadeSub);
        } else {
            TryScreenFadeFunc(&screen->activeMain, fadeMain);
        }
        break;
    }

    if (screen->activeMain == FALSE && screen->activeSub == FALSE) {
        return TRUE;
    }

    return FALSE;
}

static void TryScreenFadeFunc(BOOL *running, ScreenFade *fade)
{
    if (*running && CallScreenFadeFunc(fade) == TRUE) {
        *running = FALSE;
    }
}

static BOOL CallScreenFadeFunc(ScreenFade *fade)
{
    return sScreenFadeFuncs[fade->type](fade);
}

static void SetupScreenFadeParams(enum FadeMode mode, ScreenFadeParams *params)
{
    switch (mode) {
    case FADE_BOTH_SCREENS:
        InitScreenFadeParams(params, ORDER_SIMULTANEOUS, TRUE, TRUE);
        break;

    case FADE_MAIN_THEN_SUB:
        InitScreenFadeParams(params, ORDER_MAIN_FIRST, TRUE, TRUE);
        break;

    case FADE_SUB_THEN_MAIN:
        InitScreenFadeParams(params, ORDER_SUB_FIRST, TRUE, TRUE);
        break;

    case FADE_MAIN_ONLY:
        InitScreenFadeParams(params, ORDER_MAIN_FIRST, TRUE, FALSE);
        break;

    case FADE_SUB_ONLY:
        InitScreenFadeParams(params, ORDER_SUB_FIRST, FALSE, TRUE);
        break;
    }
}

static void InitScreenFadeParams(ScreenFadeParams *params, enum ScreenFadeOrder order, BOOL fadeMain, BOOL fadeSub)
{
    params->order = order;
    params->activeMain = fadeMain;
    params->activeSub = fadeSub;
    params->existsMain = fadeMain;
    params->existsSub = fadeSub;
}

static void InitScreenFade(ScreenFade *fade, enum FadeType type, int steps, int framesPerStep, enum FadeState state, void *data, enum DSScreen screen, HardwareWindowSettings *hwSettings, ScreenFadeHBlanks *hblanks, enum HeapID heapID, u16 color)
{
    fade->type = type;
    fade->steps = steps;
    fade->framesPerStep = framesPerStep;
    fade->state = state;
    fade->data = data;
    fade->screen = screen;
    fade->hwSettings = hwSettings;
    fade->hblanks = hblanks;
    fade->heapID = heapID;
    fade->color = color;
}

static void ClearHBlanks(ScreenFadeHBlanks *hblanks)
{
    for (int screen = 0; screen < DS_SCREEN_MAX; screen++) {
        hblanks->data[screen] = NULL;
        hblanks->callback[screen] = DummyHBlankCallback;
        hblanks->running[screen] = FALSE;
    }
}

static void RunHBlankCallbacks(void *data)
{
    ScreenFadeHBlanks *hblanks = data;
    for (int screen = 0; screen < DS_SCREEN_MAX; screen++) {
        hblanks->callback[screen](hblanks->data[screen]);
    }
}

static void EnableScreenHBlank(ScreenFadeHBlanks *hblanks, void *data, Callback callback, enum DSScreen screen)
{
    u8 validCallback = TRUE;
    GF_ASSERT(hblanks->running[screen] == FALSE);
    GF_ASSERT(hblanks->callback[screen] != NULL);

    if (hblanks->running[DS_SCREEN_MAIN] == FALSE && hblanks->running[DS_SCREEN_SUB] == FALSE) {
        validCallback = SetHBlankCallback(RunHBlankCallbacks, hblanks);
    }

    GF_ASSERT(validCallback == TRUE);

    hblanks->data[screen] = data;

    if (callback) {
        hblanks->callback[screen] = callback;
    } else {
        hblanks->callback[screen] = DummyHBlankCallback;
    }

    hblanks->running[screen] = TRUE;
}

static void DisableScreenHBlank(ScreenFadeHBlanks *hblanks, enum DSScreen screen)
{
    hblanks->running[screen] = FALSE;

    if (hblanks->running[DS_SCREEN_MAIN] == FALSE && hblanks->running[DS_SCREEN_SUB] == FALSE) {
        DisableHBlank();
    }

    hblanks->callback[screen] = DummyHBlankCallback;
    hblanks->data[screen] = NULL;
}

void RequestEnableScreenHBlank(ScreenFadeHBlanks *hblanks, void *data, Callback callback, enum DSScreen screen, enum HeapID heapID)
{
    EnableHBlankTemplate *template = Heap_AllocAtEnd(heapID, sizeof(EnableHBlankTemplate));
    template->hblanks = hblanks;
    template->data = data;
    template->callback = callback;
    template->screen = screen;

    SysTask_ExecuteAfterVBlank(Task_EnableScreenHBlank, template, LOCAL_TASK_PRIORIITY);
}

void RequestDisableScreenHBlank(ScreenFadeHBlanks *hblanks, enum DSScreen screen, enum HeapID heapID)
{
    DisableHBlankTemplate *template = Heap_AllocAtEnd(heapID, sizeof(DisableHBlankTemplate));
    template->hblanks = hblanks;
    template->screen = screen;

    SysTask_ExecuteAfterVBlank(Task_DisableScreenHBlank, template, LOCAL_TASK_PRIORIITY);
}

static void Task_EnableScreenHBlank(SysTask *task, void *data)
{
    EnableHBlankTemplate *template = data;
    EnableScreenHBlank(template->hblanks, template->data, template->callback, template->screen);
    SysTask_Done(task);
    Heap_Free(data);
}

static void Task_DisableScreenHBlank(SysTask *task, void *data)
{
    DisableHBlankTemplate *template = data;
    DisableScreenHBlank(template->hblanks, template->screen);
    SysTask_Done(task);
    Heap_Free(data);
}

static void DummyHBlankCallback(void *data)
{
    return;
}

static u16 GetFadeColor(ScreenFadeManager *manager, u16 color)
{
    if (color == COLOR_SAVED) {
        return manager->savedColor;
    }

    return color;
}

static u16 GetSavedFadeColor(const ScreenFadeManager *manager)
{
    const ScreenFade *fade;
    if (manager->screen.existsMain == TRUE) {
        fade = &manager->mainScreenFade;
    } else {
        fade = &manager->subScreenFade;
    }

    if (fade->direction == FADE_OUT) {
        return fade->color;
    }

    return manager->savedColor;
}

static void Task_ResetScreenMasterBrightness(SysTask *task, void *data)
{
    ScreenFade *fade = data;
    SetScreenMasterBrightness(fade->screen, 0);
    SysTask_Done(task);
}

static void RequestResetScreenMasterBrightness(ScreenFade *fade)
{
    if (fade->direction == FADE_IN
        && (fade->color == COLOR_WHITE || fade->color == COLOR_BLACK)
        && fade->method == FADE_BY_WINDOW) {
        SysTask_ExecuteAfterVBlank(Task_ResetScreenMasterBrightness, fade, LOCAL_TASK_PRIORIITY);
    }
}

static void ResetWindowScreenFade(ScreenFade *fade)
{
    if (fade->direction == FADE_OUT
        && (fade->color == COLOR_WHITE || fade->color == COLOR_BLACK)
        && fade->method == FADE_BY_WINDOW) {
        SetScreenColorBrightness(fade->screen, fade->color);
        ResetVisibleHardwareWindows(fade->screen);
    }
}

static void ZeroScreenFadeManager(ScreenFadeManager *manager)
{
    memset(&manager->screen, 0, sizeof(ScreenFadeParams));
    memset(&manager->mainScreenFade, 0, sizeof(ScreenFade));
    memset(&manager->subScreenFade, 0, sizeof(ScreenFade));
    memset(&manager->hblanks, 0, sizeof(ScreenFadeHBlanks));
    memset(&manager->hwSettings, 0, sizeof(HardwareWindowSettings));
}
