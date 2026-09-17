/* Diagnostic replays only (make HATCH_NOW=1): on the first field step that reaches the daycare
 * update, zero the egg cycles (MON_DATA_EGG_CYCLES shares the friendship slot) of the first
 * party egg and report "hatch now", so the game runs std_hatch_egg -> HatchEggApp without
 * walking thousands of steps. Wraps HandleDaycareStep (called cross-object from
 * field_control.c FieldSystem_UpdateDaycare). Never part of a release link. */
#include <nitro.h>
#include <stdio.h>
#include "field_system.h"
#include "daycare.h"
#include "party.h"
#include "pokemon.h"
#include "constants/pokemon.h"

extern void VitaNativeMemLog(const char *fmt, ...);
extern BOOL __real_HandleDaycareStep(Daycare *dayCare, Party *party, FieldSystem *fieldSystem);

BOOL __wrap_HandleDaycareStep(Daycare *dayCare, Party *party, FieldSystem *fieldSystem)
{
    static int done;
    if (!done) {
        int n = Party_GetCount(party);
        for (int i = 0; i < n; i++) {
            Pokemon *mon = Party_GetMonByIndex(party, i);
            if (GetMonData(mon, MON_DATA_IS_EGG, NULL) && !GetMonData(mon, MON_DATA_CHECKSUM_FAILED, NULL)) {
                u32 zero = 0;
                SetMonData(mon, MON_DATA_EGG_CYCLES, &zero);
                done = 1;
                VitaNativeMemLog("[DIAG] hatch now: party slot %d species %u", i,
                                (unsigned)GetMonData(mon, MON_DATA_SPECIES, NULL));
                printf("[DIAG] hatch now: party slot %d\n", i);
                return TRUE; /* FieldSystem_UpdateDaycare then starts std_hatch_egg */
            }
        }
    }
    return __real_HandleDaycareStep(dayCare, party, fieldSystem);
}
