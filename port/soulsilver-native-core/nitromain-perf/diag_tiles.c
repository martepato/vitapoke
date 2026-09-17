/* Diagnostic replays only (make DIAG_TILES=1): wraps GetMetatileBehavior (defined in unk_02054648.c and
 * called from other objects) and logs each distinct (x, z, behaviour, solid) the game queries, capped, so a
 * replay shows the port's own tile data around the player. Never part of a release link. */
#include <nitro.h>
#include <stdio.h>
#include "field_system.h"

extern void VitaNativeMemLog(const char *fmt, ...);
extern u8 __real_GetMetatileBehavior(FieldSystem *fieldSystem, int x, int z);
extern BOOL sub_020548C0(FieldSystem *fieldSystem, int x, int z);

u8 __wrap_GetMetatileBehavior(FieldSystem *fieldSystem, int x, int z)
{
    static int count;
    static u32 seen[400];
    u8 behavior = __real_GetMetatileBehavior(fieldSystem, x, z);
    u32 key = ((u32)(x & 0xFFF) << 20) | ((u32)(z & 0xFFF) << 8) | behavior;
    for (int i = 0; i < count; i++) {
        if (seen[i] == key) return behavior;
    }
    if (count < (int)(sizeof(seen) / sizeof(seen[0]))) {
        BOOL solid = sub_020548C0(fieldSystem, x, z);
        seen[count++] = key;
        VitaNativeMemLog("[DIAG-TILE] x=%d z=%d beh=%u solid=%d", x, z, (unsigned)behavior, solid);
        printf("[DIAG-TILE] x=%d z=%d beh=%u solid=%d\n", x, z, (unsigned)behavior, solid);
    }
    return behavior;
}
