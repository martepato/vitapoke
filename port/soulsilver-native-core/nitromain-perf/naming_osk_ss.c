/* naming_osk_ss.c - SoulSilver half of the name-entry bypass (see the Platinum
 * original, native-stack-app/naming_osk.c, for the reasoning).
 *
 * The DS naming screen (gOverlayTemplate_NamingScreen) is the one screen that
 * killed a real PSP-3001 on Platinum; nothing suggests SoulSilver's copy is
 * kinder, so it is never constructed here. __wrap_OverlayManager_New swaps the
 * template for the one below. Every naming context - player, rival, nickname,
 * PC box, group, hatched egg - reaches OverlayManager_New (directly, or via
 * CallApplicationAsTask -> FieldSystem_LaunchApplication), so one interception
 * covers them all.
 *
 * On exit it fills in exactly what NamingScreenApp_Exit would have filled in:
 *
 *   args->nameInputFlat    u16[20] charcodes, EOS terminated
 *   args->nameInputString  the same text as a managed String
 *   args->noInput          TRUE when the caller must keep its old name
 *
 * The PSP side (firmware keyboard, drawn from the renderer's Present) is osk.c.
 */
#include "global.h"
#include "overlay_manager.h"
#include "pm_string.h"
#include "msgdata/msg.naix"
#include "msgdata/msg/msg_0254.h"
#include "constants/charcode.h"
#include "math_util.h"
#include "msgdata.h"
#include "string_util.h"

#include <string.h>

/* naming_screen.h drags in the whole application header tree (spl.h and
 * friends the native build does not carry), so the two things needed from it
 * are mirrored here. Layout must match game-include/naming_screen.h. */
typedef enum { NAME_SCREEN_PLAYER, NAME_SCREEN_POKEMON, NAME_SCREEN_BOX, NAME_SCREEN_RIVAL, NAME_SCREEN_UNK4, NAME_SCREEN_GROUP } NameScreenType;
typedef struct NamingScreenArgs {
    NameScreenType kind;
    int playerGenderOrMonSpecies;
    int monForm;
    int maxLen;
    int monGender;
    BOOL noInput;
    String *nameInputString;
    u16 nameInputFlat[20];
    /* battleMsgId, pcStorage, options, pMenuInputState follow; untouched here. */
} NamingScreenArgs;
extern const OverlayManagerTemplate gOverlayTemplate_NamingScreen;

extern void VitaNativeOskBegin(const unsigned short *desc, const unsigned short *intext, int limit);
extern int VitaNativeOskFinished(void);
extern int VitaNativeOskAccepted(void);
extern const unsigned short *VitaNativeOskText(void);
extern void VitaNativeOskRelease(void);
extern void VitaNativeMemLog(const char *fmt, ...);

/* NamingScreenArgs::nameInputFlat[20] is the widest buffer the game hands us. */
#define OSK_NAME_CAP 19

/* Only characters the DS keyboard pages (naming_screen.c sKeyboardRow_*) can
 * produce; anything else the PSP keyboard yields is dropped, so every name
 * from this path is one the original screen could have typed. */
static const struct { u16 cc; u16 uc; } sCharMap[] = {
    { CHAR_SPACE, ' ' },      { CHAR_COMMA, ',' },     { CHAR_PERIOD, '.' },
    { CHAR_COLON, ':' },      { CHAR_SEMICOLON, ';' }, { CHAR_EXCL, '!' },
    { CHAR_QMARK, '?' },      { CHAR_LDQUOT, '"' },    { CHAR_RDQUOT, 0x201D },
    { CHAR_LAPOST, 0x2018 },  { CHAR_RAPOST, '\'' },   { CHAR_LPAREN, '(' },
    { CHAR_RPAREN, ')' },     { CHAR_ELLIPSIS, 0x2026 }, { CHAR_CTRDOT, 0x00B7 },
    { CHAR_TILDE, '~' },      { CHAR_AT, '@' },        { CHAR_HASH, '#' },
    { CHAR_PERCENT, '%' },    { CHAR_PLUS, '+' },      { CHAR_HYPHEN, '-' },
    { CHAR_ASTERISK, '*' },   { CHAR_SLASH, '/' },     { CHAR_EQUALS, '=' },
    { CHAR_MALE, 0x2642 },    { CHAR_FEMALE, 0x2640 },
};

static u16 UnicodeToCharCode(unsigned short uc)
{
    u32 i;
    if (uc >= '0' && uc <= '9') return (u16)(CHAR_0 + (uc - '0'));
    if (uc >= 'A' && uc <= 'Z') return (u16)(CHAR_A + (uc - 'A'));
    if (uc >= 'a' && uc <= 'z') return (u16)(CHAR_a + (uc - 'a'));
    for (i = 0; i < NELEMS(sCharMap); i++) if (sCharMap[i].uc == uc) return sCharMap[i].cc;
    return EOS;
}

static unsigned short CharCodeToUnicode(u16 cc)
{
    u32 i;
    if (cc >= CHAR_0 && cc <= CHAR_9) return (unsigned short)('0' + (cc - CHAR_0));
    if (cc >= CHAR_A && cc <= CHAR_Z) return (unsigned short)('A' + (cc - CHAR_A));
    if (cc >= CHAR_a && cc <= CHAR_z) return (unsigned short)('a' + (cc - CHAR_a));
    for (i = 0; i < NELEMS(sCharMap); i++) if (sCharMap[i].cc == cc) return sCharMap[i].uc;
    return 0;
}

static const char *PromptForKind(NameScreenType kind)
{
    switch (kind) {
    case NAME_SCREEN_PLAYER:  return "Your name?";
    case NAME_SCREEN_RIVAL:   return "Your rival's name?";
    case NAME_SCREEN_POKEMON: return "Nickname?";
    case NAME_SCREEN_BOX:     return "Box name?";
    case NAME_SCREEN_GROUP:   return "Group name?";
    default:                  return "Enter a name.";
    }
}

typedef struct OskNamingApp {
    u16 initial[OSK_NAME_CAP + 1];
    int maxLen;
} OskNamingApp;

static BOOL OskNaming_Init(OverlayManager *ovyMan, int *state)
{
    NamingScreenArgs *args = OverlayManager_GetArgs(ovyMan);
    OskNamingApp *app;
    unsigned short desc[64], intext[OSK_NAME_CAP + 1];
    const char *prompt = PromptForKind(args->kind);
    int i, n = 0;

    (void)state;
    /* The DS screen owns this heap (created in NamingScreenApp_Init, destroyed in
     * its Exit); the default-name loader below expects it too. Far smaller than
     * the 0x28000 the DS screen needs. */
    Heap_Create(HEAP_ID_3, HEAP_ID_NAMING_SCREEN, 0x2000);
    app = OverlayManager_CreateAndGetData(ovyMan, sizeof(OskNamingApp), HEAP_ID_NAMING_SCREEN);
    memset(app, 0, sizeof(*app));
    app->maxLen = args->maxLen < 1 ? 1 : args->maxLen > OSK_NAME_CAP ? OSK_NAME_CAP : args->maxLen;
    for (i = 0; prompt[i] && i < 63; i++) desc[i] = (unsigned short)(unsigned char)prompt[i];
    desc[i] = 0;

    /* The DS screen seeds entryBufBak from nameInputString: that is both the
     * keyboard's initial text and the value meaning "nothing changed". */
    app->initial[0] = EOS;
    if (args->nameInputString != NULL) {
        u16 raw[32];
        CopyStringToU16Array(args->nameInputString, raw, 32);
        for (i = 0; raw[i] != EOS && n < app->maxLen; i++) {
            unsigned short uc = CharCodeToUnicode(raw[i]);
            app->initial[n] = raw[i];
            intext[n] = uc ? uc : ' ';
            n++;
        }
        app->initial[n] = EOS;
    }
    intext[n] = 0;

    VitaNativeMemLog("[OSK] naming kind=%d maxLen=%d initial=%d chars", (int)args->kind, app->maxLen, n);
    VitaNativeOskBegin(desc, intext, app->maxLen);
    return TRUE;
}

static BOOL OskNaming_Main(OverlayManager *ovyMan, int *state)
{
    (void)ovyMan; (void)state;
    return VitaNativeOskFinished() ? TRUE : FALSE;
}

/* NamingScreen_SetDefaultName without the app struct: a random generic name
 * for the player and rival (SoulSilver rows of msg_0254), noInput otherwise. */
static void UseDefaultName(NamingScreenArgs *args)
{
    if (args->kind == NAME_SCREEN_PLAYER || args->kind == NAME_SCREEN_RIVAL) {
        MsgData *msg = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, NARC_msg_msg_0254_bin, HEAP_ID_NAMING_SCREEN);
        s32 entry;
        String *string;
        if (args->kind == NAME_SCREEN_RIVAL) entry = msg_0254_00085;
        else if (args->playerGenderOrMonSpecies == PLAYER_GENDER_FEMALE) entry = msg_0254_00060 + (LCRandom() % 18);
        else entry = msg_0254_00042 + (LCRandom() % 18);
        string = NewString_ReadMsgData(msg, entry);
        String_Copy(args->nameInputString, string);
        String_Delete(string);
        CopyStringToU16Array(args->nameInputString, args->nameInputFlat, 10);
        DestroyMsgData(msg);
        VitaNativeMemLog("[OSK] no input; generic name entry %d used", (int)entry);
    } else {
        args->noInput = TRUE;
        VitaNativeMemLog("[OSK] no input; noInput for kind %d", (int)args->kind);
    }
}

static BOOL IsAllSpaces(const u16 *s)
{
    for (; *s != EOS; s++) if (*s != CHAR_SPACE) return FALSE;
    return TRUE;
}

static BOOL OskNaming_Exit(OverlayManager *ovyMan, int *state)
{
    NamingScreenArgs *args = OverlayManager_GetArgs(ovyMan);
    OskNamingApp *app = OverlayManager_GetData(ovyMan);
    u16 entry[OSK_NAME_CAP + 1];
    int n = 0;

    (void)state;
    if (VitaNativeOskAccepted()) {
        const unsigned short *text = VitaNativeOskText();
        int i;
        for (i = 0; text[i] != 0 && n < app->maxLen; i++) {
            u16 cc = UnicodeToCharCode(text[i]);
            if (cc != EOS) entry[n++] = cc;
        }
    }
    entry[n] = EOS;

    /* The three rejections NamingScreenApp_Exit applies. */
    if (n == 0 || !StringNotEqual(entry, app->initial) || IsAllSpaces(entry)) {
        UseDefaultName(args);
    } else {
        CopyU16StringArray(args->nameInputFlat, entry);
        CopyU16ArrayToString(args->nameInputString, entry);
        VitaNativeMemLog("[OSK] accepted %d chars for naming kind %d", n, (int)args->kind);
    }

    VitaNativeOskRelease();
    OverlayManager_FreeData(ovyMan);
    Heap_Destroy(HEAP_ID_NAMING_SCREEN);
    return TRUE;
}

static const OverlayManagerTemplate sOskNamingTemplate = {
    OskNaming_Init, OskNaming_Main, OskNaming_Exit, FS_OVERLAY_ID_NONE,
};

extern OverlayManager *__real_OverlayManager_New(const OverlayManagerTemplate *template, void *parentWork, enum HeapID heapID);

OverlayManager *__wrap_OverlayManager_New(const OverlayManagerTemplate *template, void *parentWork, enum HeapID heapID)
{
#ifndef VITAPOKE_KEEP_DS_NAMING
    if (template == &gOverlayTemplate_NamingScreen) {
        VitaNativeMemLog("[OSK] substituting PSP keyboard for gOverlayTemplate_NamingScreen");
        template = &sOskNamingTemplate;
    }
#endif
    return __real_OverlayManager_New(template, parentWork, heapID);
}
