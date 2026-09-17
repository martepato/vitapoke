/* Diagnostic replays only (make WARP_TO=<mapHeaderId>,<x>,<z>[,<dir>]): once the field has run for
 * 90 frames with no task active, queue the game's own map-change task to that map/tile, so any
 * map can be tested without walking there. Hooked on FieldTask_Run, which field_system.c calls
 * every field frame from another object (so the link-time --wrap binds). Never part of a
 * release link; scripts/platinum.sh links without it. Map ids: the MAP_HEADER_* enum in the
 * decompilation (Oreburgh City = 45, Oreburgh Mine B1F = 198). */
#include <nitro.h>
#include <stdlib.h>
#include <stdio.h>
#include "field_task.h"

extern void VitaNativeMemLog(const char *fmt, ...);
extern BOOL __real_FieldTask_Run(FieldSystem *fieldSystem);
extern void FieldTask_StartMapChangeFull(FieldTask *task, int mapHeaderID, int warpId, int x, int z, int dir);

/* Tick 1 queues the map change as a child task and yields; tick 2 (after the change finished)
 * ends the task. Same shape as the script engine's Warp command. */
static BOOL DiagWarpTask(FieldTask *task)
{
    static int state;
    if (state == 0) {
        int map, x, z, dir = 1; const char *s = WARP_TO;
        map = (int)strtol(s, (char **)&s, 10); if (*s == ',') s++;
        x = (int)strtol(s, (char **)&s, 10);   if (*s == ',') s++;
        z = (int)strtol(s, (char **)&s, 10);   if (*s == ',') { s++; dir = (int)strtol(s, (char **)&s, 10); }
        VitaNativeMemLog("[DIAG] warp to map %d (%d,%d) dir %d", map, x, z, dir);
        printf("[DIAG] warp to map %d (%d,%d) dir %d\n", map, x, z, dir);
        state = 1;
        FieldTask_StartMapChangeFull(task, map, -1, x, z, dir);
        return FALSE;
    }
    printf("[DIAG] warp done\n");
    return TRUE;
}

BOOL __wrap_FieldTask_Run(FieldSystem *fieldSystem)
{
    static int frames, done;
    if (!done && !FieldSystem_IsRunningTask(fieldSystem) && ++frames == 90) {
        done = 1;
        FieldSystem_CreateTask(fieldSystem, DiagWarpTask, NULL);
    }
    return __real_FieldTask_Run(fieldSystem);
}
