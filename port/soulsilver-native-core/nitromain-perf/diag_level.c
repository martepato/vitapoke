/* Diagnostic replays only (make PARTY_LEVEL=<n>): once the field runs, raise every party
 * member to level n, recalculate stats and heal to full, so story battles past the player's
 * real strength (e.g. Falkner) can be tested without grinding. Same hook as diag_warp.c
 * (PlayerAvatar_UpdateMovement), so the two switches are mutually exclusive. Never part of a
 * release link. */
#include <nitro.h>
#include <stdio.h>
#include "field_system.h"
#include "player_avatar.h"
#include "map_object.h"
#include "party.h"
#include "pokemon.h"
#include "constants/pokemon.h"

extern void VitaNativeMemLog(const char *fmt, ...);
extern void __real_PlayerAvatar_UpdateMovement(PlayerAvatar *avatar);

static void DiagBoostParty(FieldSystem *fieldSystem)
{
    Party *party = SaveArray_Party_Get(FieldSystem_GetSaveData(fieldSystem));
    int n = Party_GetCount(party);
    for (int i = 0; i < n; i++) {
        Pokemon *mon = Party_GetMonByIndex(party, i);
        int species = (int)GetMonData(mon, MON_DATA_SPECIES, NULL);
        if (species == 0) continue;
#ifdef PARTY_LEVEL_EDGE
        u32 exp = GetMonExpBySpeciesAndLevel(species, PARTY_LEVEL + 1) - 1; /* one point short of the next level */
#else
        u32 exp = GetMonExpBySpeciesAndLevel(species, PARTY_LEVEL);
#endif
        SetMonData(mon, MON_DATA_EXPERIENCE, &exp);
        CalcMonLevelAndStats(mon);
        u32 maxHp = GetMonData(mon, MON_DATA_MAX_HP, NULL);
        SetMonData(mon, MON_DATA_HP, &maxHp);
        VitaNativeMemLog("[DIAG] party slot %d species %d -> level %u, hp %u", i, species,
                        (unsigned)GetMonData(mon, MON_DATA_LEVEL, NULL), (unsigned)maxHp);
        printf("[DIAG] party slot %d species %d -> level %u\n", i, species,
               (unsigned)GetMonData(mon, MON_DATA_LEVEL, NULL));
    }
}

void __wrap_PlayerAvatar_UpdateMovement(PlayerAvatar *avatar)
{
    static int frames, done;
    if (!done && ++frames == 30) {
        done = 1;
        DiagBoostParty(MapObject_GetFieldSystem(PlayerAvatar_GetMapObject(avatar)));
    }
    __real_PlayerAvatar_UpdateMovement(avatar);
}
