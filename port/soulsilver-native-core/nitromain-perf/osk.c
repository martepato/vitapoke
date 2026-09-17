/* PSP on-screen keyboard driver for the native Platinum port.
 *
 * Why this exists: the DS name-entry screen (gNamingScreenAppTemplate) renders
 * correctly in PPSSPP but black-screens and powers off a real PSP-3001. The
 * naming application is therefore replaced (see naming_osk.c) by a dialog the
 * PSP firmware itself draws.
 *
 * The utility dialog is not something you can block on inside a game callback:
 * it has to be ticked once per displayed frame, after the frame's GE work has
 * been flushed and before the framebuffer is handed to the display. That is
 * exactly one point in this port, inside the renderer's Present(), which calls
 * VitaNativeOskFrameHook() through a weak symbol. Everything here runs on the
 * main thread; there is no locking because there is no second caller.
 *
 * This file deliberately includes only PSP headers. The game-side half lives
 * in naming_osk.c and includes only game headers; they meet through the few
 * plain-int/u16 entry points declared at the bottom of this comment block:
 *
 *   VitaNativeOskBegin(desc, intext, limit)  request a keyboard
 *   VitaNativeOskIsActive()                  dialog owns the screen
 *   VitaNativeOskFinished()                  result is ready to collect
 *   VitaNativeOskAccepted()                  1 = user confirmed, 0 = cancelled
 *   VitaNativeOskText()                      UTF-16 result, NUL terminated
 *   VitaNativeOskRelease()                   result consumed, back to idle
 */
#include <pspkernel.h>
#include <psputility.h>
#include <string.h>

/* Longest naming context in the game is MON_NAME_LEN (10); 24 leaves room for
 * every context plus the terminator with a wide margin. */
#define PSP_OSK_MAX_CHARS 24
#define PSP_OSK_DESC_CHARS 64

/* The firmware writes into these, so they are static (never stack) and
 * 16-byte aligned like every other buffer this port hands to a system call. */
static SceUtilityOskParams sParams __attribute__((aligned(16)));
static SceUtilityOskData sData __attribute__((aligned(16)));
static unsigned short sDesc[PSP_OSK_DESC_CHARS] __attribute__((aligned(16)));
static unsigned short sIn[PSP_OSK_MAX_CHARS + 1] __attribute__((aligned(16)));
static unsigned short sOut[PSP_OSK_MAX_CHARS + 1] __attribute__((aligned(16)));

enum OskState {
    OSK_IDLE = 0, /* nothing requested */
    OSK_PENDING,  /* requested, InitStart not issued yet */
    OSK_RUNNING,  /* firmware dialog is up */
    OSK_DONE      /* result sitting in sOut, waiting for the game to take it */
};

static int sState;
static int sAccepted;
static int sSawVisible;
static unsigned sStateFrames;

/* Safety valve. If the dialog never becomes visible the game would otherwise
 * wait forever on a screen the user cannot dismiss. Give up instead and report
 * a cancel, which makes the game fall back to its own default name. These are
 * frames of the port's ~30 fps loop, so 600 is about 20 seconds. */
#define OSK_NO_SHOW_LIMIT 600
#define OSK_ABANDON_LIMIT 900

extern void VitaNativeMemLog(const char *fmt, ...);

/* Length of a NUL-terminated UTF-16 string, without pulling in wide-char libc. */
static int Utf16Len(const unsigned short *s)
{
    int n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

int VitaNativeOskIsActive(void)
{
    return sState != OSK_IDLE;
}

int VitaNativeOskFinished(void)
{
    return sState == OSK_DONE;
}

int VitaNativeOskAccepted(void)
{
    return sAccepted;
}

const unsigned short *VitaNativeOskText(void)
{
    return sOut;
}

void VitaNativeOskRelease(void)
{
    sState = OSK_IDLE;
    sAccepted = 0;
    sSawVisible = 0;
    sStateFrames = 0;
}

void VitaNativeOskBegin(const unsigned short *desc, const unsigned short *intext, int limit)
{
    int i;

    if (limit < 1) {
        limit = 1;
    }
    if (limit > PSP_OSK_MAX_CHARS) {
        limit = PSP_OSK_MAX_CHARS;
    }

    memset(sDesc, 0, sizeof(sDesc));
    memset(sIn, 0, sizeof(sIn));
    memset(sOut, 0, sizeof(sOut));

    for (i = 0; desc && desc[i] && i < PSP_OSK_DESC_CHARS - 1; i++) {
        sDesc[i] = desc[i];
    }
    for (i = 0; intext && intext[i] && i < limit; i++) {
        sIn[i] = intext[i];
    }

    memset(&sData, 0, sizeof(sData));
    sData.language = PSP_UTILITY_OSK_LANGUAGE_ENGLISH;
    sData.lines = 1;
    sData.inputtype = PSP_UTILITY_OSK_INPUTTYPE_LATIN_DIGIT
        | PSP_UTILITY_OSK_INPUTTYPE_LATIN_SYMBOL
        | PSP_UTILITY_OSK_INPUTTYPE_LATIN_LOWERCASE
        | PSP_UTILITY_OSK_INPUTTYPE_LATIN_UPPERCASE;
    sData.desc = sDesc;
    sData.intext = sIn;
    sData.outtextlength = PSP_OSK_MAX_CHARS + 1;
    sData.outtextlimit = limit;
    sData.outtext = sOut;
    sData.result = PSP_UTILITY_OSK_RESULT_UNCHANGED;

    memset(&sParams, 0, sizeof(sParams));
    sParams.base.size = sizeof(sParams);
    /* Take the dialog's UI language and its confirm button from the console's
     * own settings, so the keyboard behaves like every other PSP dialog the
     * user has. (The keyboard charset above stays English: the game's charcode
     * table only has the western character set.) Also keeps the whole
     * libpsputility stub group contiguous at link time, which psp-fixup-imports
     * requires - sceUtilityGetSystemParamInt is pulled in by libcglue anyway. */
    {
        int value = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
        if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &value) != 0) {
            value = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
        }
        sParams.base.language = value;

        value = PSP_UTILITY_ACCEPT_CROSS;
        if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_BUTTON_SWAP, &value) != 0) {
            value = PSP_UTILITY_ACCEPT_CROSS;
        }
        sParams.base.buttonSwap = value;
    }
    /* All four must be a higher priority (lower number) than this thread, which
     * is the pspsdk default user main thread at 0x20, or the dialog starves. */
    sParams.base.graphicsThread = 17;
    sParams.base.accessThread = 19;
    sParams.base.fontThread = 18;
    sParams.base.soundThread = 16;
    sParams.datacount = 1;
    sParams.data = &sData;

    sAccepted = 0;
    sSawVisible = 0;
    sStateFrames = 0;

#ifdef VITAPOKE_OSK_AUTONAME
    /* TEST ONLY: substitute a fixed name without opening the dialog, so an
     * unattended headless probe (which cannot press buttons on the emulated
     * PSP pad, and so cannot drive the firmware keyboard) can still exercise
     * the whole conversion and write-back path. Never build a release with
     * VITAPOKE_OSK_AUTONAME. */
    {
        static const char autoName[] = VITAPOKE_OSK_AUTONAME;
        for (i = 0; autoName[i] && i < limit; i++) {
            sOut[i] = (unsigned short)(unsigned char)autoName[i];
        }
        sOut[i] = 0;
        sAccepted = 1;
        sState = OSK_DONE;
        VitaNativeMemLog("[OSK] AUTONAME test path, no dialog: \"%s\" limit=%d", autoName, limit);
        return;
    }
#endif

    sState = OSK_PENDING;
    VitaNativeMemLog("[OSK] requested limit=%d initial_len=%d", limit, Utf16Len(sIn));
}

void VitaNativeOskFrameHook(void)
{
    sStateFrames++;

    switch (sState) {
    case OSK_PENDING: {
        int rc = sceUtilityOskInitStart(&sParams);
        if (rc < 0) {
            VitaNativeMemLog("[OSK] sceUtilityOskInitStart failed %08x; using default name", rc);
            sAccepted = 0;
            sState = OSK_DONE;
        } else {
            sState = OSK_RUNNING;
            sStateFrames = 0;
        }
        break;
    }

    case OSK_RUNNING:
        switch (sceUtilityOskGetStatus()) {
        case PSP_UTILITY_DIALOG_INIT:
            break;

        case PSP_UTILITY_DIALOG_VISIBLE:
            if (!sSawVisible) {
                sSawVisible = 1;
                VitaNativeMemLog("[OSK] dialog visible at frame %u", sStateFrames);
            }
            sceUtilityOskUpdate(1);
            break;

        case PSP_UTILITY_DIALOG_QUIT:
            sceUtilityOskShutdownStart();
            break;

        case PSP_UTILITY_DIALOG_FINISHED:
            break;

        case PSP_UTILITY_DIALOG_NONE:
        default:
            sAccepted = (sData.result != PSP_UTILITY_OSK_RESULT_CANCELLED)
                && (sParams.base.result == 0) && sSawVisible;
            VitaNativeMemLog("[OSK] closed field_result=%d base_result=%08x accepted=%d",
                sData.result, sParams.base.result, sAccepted);
            sState = OSK_DONE;
            break;
        }

        if (!sSawVisible) {
            if (sStateFrames == OSK_NO_SHOW_LIMIT) {
                VitaNativeMemLog("[OSK] never became visible after %u frames; shutting down",
                    sStateFrames);
                sceUtilityOskShutdownStart();
            } else if (sStateFrames >= OSK_ABANDON_LIMIT) {
                VitaNativeMemLog("[OSK] abandoned after %u frames; using default name",
                    sStateFrames);
                sAccepted = 0;
                sState = OSK_DONE;
            }
        }
        break;

    default:
        break;
    }
}
