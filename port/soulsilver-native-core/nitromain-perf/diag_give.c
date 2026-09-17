/* Diagnostic replays only. All four switches share one wrap of HandleDaycareStep (cross-object call
 * from field_control.c), run once on the first ordinary field step, and COMBINE with WARP_TO and
 * PARTY_LEVEL; all are exclusive with HATCH_NOW (same wrapped symbol). Never in a release link.
 *   GIVE_SPECIES=<species>[ GIVE_LEVEL=<n>]  append one Pokemon to the party
 *   GIVE_ITEMS=1                             Old Rod, Bicycle, HM01-HM08 into the bag
 *   GIVE_BADGES=<mask>                       OR badge bits 0-15 (0xFF = all Johto)
 *   TEACH_MOVES=<slot>:<move>[,<slot>:<move>...]  numeric move ids, e.g. 0:15,0:57,1:249,1:70
 *   GIVE_ITEM_IDS=<item>[:<count>][,...]      numeric item ids, e.g. 78:3,79:5,50:2
 *   SET_VARS=<var>:<value>[,...]              script vars (hex ok), e.g. 0x4079:2 opens the Ecruteak Gym
 *   GIVE_FRIENDSHIP=<n> (all party mons), REPEL_STEPS=<n> [REPEL_FULL=100|200|250] (SS QoL repel prompt tests)
 *   SET_FLAGS=<flag>[,...] / CLEAR_FLAGS=<flag>[,...]  save flags (hex ok), e.g. CLEAR_FLAGS=0x1D9 shows Jasmine in her gym */
#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include "field_system.h"
#include "daycare.h"
#include "party.h"
#include "pokemon.h"
#include "heap.h"
#include "bag.h"
#include "player_data.h"
#include "constants/pokemon.h"
#include "constants/items.h"
#include "save_vars_flags.h"
#include "sys_vars.h"

extern void VitaNativeMemLog(const char *fmt, ...);
extern BOOL __real_HandleDaycareStep(Daycare *dayCare, Party *party, FieldSystem *fieldSystem);
#ifndef GIVE_LEVEL
#define GIVE_LEVEL 10
#endif

#ifdef GIVE_SPECIES
#include "script_pokemon_util.h"
#include "map_header.h"
static void DiagGiveSpecies(Party *party, FieldSystem *fieldSystem)
{
    if (Party_GetCount(party) < PARTY_SIZE) {
        /* SS QoL OT fix: create through the game's GiveMon (same arguments as ScrCmd_GiveMon) so the mon has the
         * player's OT name/ID; a bare CreateMon left the OT name unterminated and the Summary screen (forget-a-move)
         * hit GF_ASSERT in CopyU16ArrayToString via BufferBoxMonOTName. */
        GiveMon(HEAP_ID_FIELD3, FieldSystem_GetSaveData(fieldSystem), GIVE_SPECIES, GIVE_LEVEL, 0, 0, 0,
                MapHeader_GetMapSec(fieldSystem->location->mapId), 24);
        VitaNativeMemLog("[DIAG] gave species %d level %d (party now %d)", GIVE_SPECIES, GIVE_LEVEL, Party_GetCount(party));
        printf("[DIAG] gave species %d level %d\n", GIVE_SPECIES, GIVE_LEVEL);
    }
}
#endif

#ifdef GIVE_ITEMS
static void DiagGiveItems(FieldSystem *fieldSystem)
{
    static const u16 items[] = { ITEM_OLD_ROD, ITEM_BICYCLE, ITEM_HM01, ITEM_HM02, ITEM_HM03,
                                 ITEM_HM04, ITEM_HM05, ITEM_HM06, ITEM_HM07, ITEM_HM08 };
    Bag *bag = Save_Bag_Get(FieldSystem_GetSaveData(fieldSystem));
    for (unsigned i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        BOOL ok = Bag_AddItem(bag, items[i], 1, HEAP_ID_FIELD3);
        VitaNativeMemLog("[DIAG] gave item %d -> %d", items[i], ok);
        printf("[DIAG] gave item %d -> %d\n", items[i], ok);
    }
}
#endif

#ifdef GIVE_BADGES
static void DiagGiveBadges(FieldSystem *fieldSystem)
{
    PlayerProfile *profile = Save_PlayerData_GetProfile(FieldSystem_GetSaveData(fieldSystem));
    for (int b = 0; b < 16; b++) {
        if ((GIVE_BADGES) & (1 << b)) PlayerProfile_SetBadgeFlag(profile, b);
    }
    VitaNativeMemLog("[DIAG] badges mask 0x%x (count now %d)", (unsigned)(GIVE_BADGES), PlayerProfile_CountBadges(profile));
    printf("[DIAG] badges mask 0x%x\n", (unsigned)(GIVE_BADGES));
}
#endif

#ifdef GIVE_ITEM_IDS
static void DiagGiveItemIds(FieldSystem *fieldSystem)
{
    Bag *bag = Save_Bag_Get(FieldSystem_GetSaveData(fieldSystem));
    const char *s = GIVE_ITEM_IDS;
    while (*s) {
        int item = (int)strtol(s, (char **)&s, 10);
        int count = 1;
        if (*s == ':') { s++; count = (int)strtol(s, (char **)&s, 10); }
        if (*s == ',') s++;
        if (item <= 0 || count <= 0) continue;
        BOOL ok = Bag_AddItem(bag, (u16)item, (u16)count, HEAP_ID_FIELD3);
        VitaNativeMemLog("[DIAG] gave item %d x%d -> %d", item, count, ok);
        printf("[DIAG] gave item %d x%d -> %d\n", item, count, ok);
    }
}
#endif

#ifdef SET_VARS
static void DiagSetVars(FieldSystem *fieldSystem)
{
    SaveVarsFlags *varsFlags = Save_VarsFlags_Get(FieldSystem_GetSaveData(fieldSystem));
    const char *s = SET_VARS;
    while (*s) {
        long var = strtol(s, (char **)&s, 0); if (*s == ':') s++;
        long value = strtol(s, (char **)&s, 0); if (*s == ',') s++;
        if (var < 0x4000 || var > 0xFFFF) continue;
        BOOL ok = SetScriptVar(varsFlags, (u16)var, (u16)value);
        VitaNativeMemLog("[DIAG] set var 0x%04lx = %ld -> %d", var, value, ok);
        printf("[DIAG] set var 0x%04lx = %ld -> %d\n", var, value, ok);
    }
}
#endif

#if defined(SET_FLAGS) || defined(CLEAR_FLAGS)
static void DiagFlags(FieldSystem *fieldSystem, const char *s, BOOL set)
{
    SaveVarsFlags *varsFlags = Save_VarsFlags_Get(FieldSystem_GetSaveData(fieldSystem));
    while (*s) {
        long flag = strtol(s, (char **)&s, 0); if (*s == ',') s++;
        if (flag <= 0 || flag > 0xFFFF) continue;
        if (set) Save_VarsFlags_SetFlagInArray(varsFlags, (u16)flag);
        else Save_VarsFlags_ClearFlagInArray(varsFlags, (u16)flag);
        BOOL now = Save_VarsFlags_CheckFlagInArray(varsFlags, (u16)flag);
        VitaNativeMemLog("[DIAG] %s flag 0x%04lx -> now %d", set ? "set" : "clear", flag, now);
        printf("[DIAG] %s flag 0x%04lx -> now %d\n", set ? "set" : "clear", flag, now);
    }
}
#endif

#ifdef TEACH_MOVES
static void DiagTeachMoves(Party *party)
{
    const char *s = TEACH_MOVES;
    static u8 nextOverwrite[PARTY_SIZE] = { 3, 3, 3, 3, 3, 3 };
    while (*s) {
        int slot = (int)strtol(s, (char **)&s, 10); if (*s == ':') s++;
        int move = (int)strtol(s, (char **)&s, 10); if (*s == ',') s++;
        if (slot < 0 || slot >= Party_GetCount(party) || move <= 0) continue;
        Pokemon *mon = Party_GetMonByIndex(party, slot);
        int dst = -1;
        for (int i = 0; i < 4; i++) {
            u32 m = GetMonData(mon, MON_DATA_MOVE1 + i, NULL);
            if (m == (u32)move) { dst = -2; break; }    /* already known */
            if (m == 0 && dst == -1) dst = i;            /* first empty move slot */
        }
        if (dst == -2) continue;
        if (dst == -1) dst = nextOverwrite[slot] > 0 ? nextOverwrite[slot]-- : 0; /* overwrite 3,2,1,0 */
        MonSetMoveInSlot(mon, (u16)move, (u8)dst);
        VitaNativeMemLog("[DIAG] party slot %d move slot %d = move %d (pp %u)", slot, dst, move,
                        (unsigned)GetMonData(mon, MON_DATA_MOVE1_PP + dst, NULL));
        printf("[DIAG] party slot %d move slot %d = move %d\n", slot, dst, move);
    }
}
#endif

#ifdef GIVE_FRIENDSHIP
static void DiagFriendship(Party *party)
{
    int n = Party_GetCount(party);
    for (int i = 0; i < n; i++) {
        Pokemon *mon = Party_GetMonByIndex(party, i);
        int f = GIVE_FRIENDSHIP;
        SetMonData(mon, MON_DATA_FRIENDSHIP, &f);
    }
    VitaNativeMemLog("[DIAG] friendship %d on %d party mons", GIVE_FRIENDSHIP, n);
    printf("[DIAG] friendship %d on %d party mons\n", GIVE_FRIENDSHIP, n);
}
#endif

#ifdef REPEL_STEPS
#include "roamer.h"
static void DiagRepel(FieldSystem *fieldSystem)
{
    RoamerSaveData *roamerSave = Save_Roamers_Get(FieldSystem_GetSaveData(fieldSystem));
    *RoamerSave_GetRepelAddr(roamerSave) = REPEL_STEPS;
    roamerSave->unk_66 = REPEL_FULL;
    VitaNativeMemLog("[DIAG] repel steps %d full %d", REPEL_STEPS, REPEL_FULL);
    printf("[DIAG] repel steps %d full %d\n", REPEL_STEPS, REPEL_FULL);
}
#endif

BOOL __wrap_HandleDaycareStep(Daycare *dayCare, Party *party, FieldSystem *fieldSystem)
{
    static int done;
    if (!done) {
        done = 1;
#ifdef GIVE_SPECIES
        DiagGiveSpecies(party, fieldSystem);
#endif
#ifdef GIVE_ITEMS
        DiagGiveItems(fieldSystem);
#endif
#ifdef GIVE_BADGES
        DiagGiveBadges(fieldSystem);
#endif
#ifdef TEACH_MOVES
        DiagTeachMoves(party);
#endif
#ifdef GIVE_ITEM_IDS
        DiagGiveItemIds(fieldSystem);
#endif
#ifdef GIVE_FRIENDSHIP
        DiagFriendship(party);
#endif
#ifdef REPEL_STEPS
        DiagRepel(fieldSystem);
#endif
#ifdef SET_VARS
        DiagSetVars(fieldSystem);
#endif
#ifdef SET_FLAGS
        DiagFlags(fieldSystem, SET_FLAGS, TRUE);
#endif
#ifdef CLEAR_FLAGS
        DiagFlags(fieldSystem, CLEAR_FLAGS, FALSE);
#endif
    }
    return __real_HandleDaycareStep(dayCare, party, fieldSystem);
}
