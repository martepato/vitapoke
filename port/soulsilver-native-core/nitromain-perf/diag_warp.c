/* Diagnostic replays only (make WARP_TO=<mapId>,<x>,<z>): once the player may move, queue the
 * game's own script-warp task to that map/tile, so any map can be tested without walking
 * there. Hooked on PlayerAvatar_UpdateMovement (called cross-object only while the movement
 * gate is open; Field_AppExec is referenced from a template in its own object, so a --wrap on
 * it never binds). Never part of a release link. */
#include <nitro.h>
#include <stdlib.h>
#include <stdio.h>
#include "field_system.h"
#include "field_warp_tasks.h"
#include "player_avatar.h"
#include "map_object.h"
#include "task.h"

extern void VitaNativeMemLog(const char *fmt, ...);
extern void __real_PlayerAvatar_UpdateMovement(PlayerAvatar *avatar);

/* Top-level field task: tick 1 queues the script warp as a child task, tick 2 (after the
 * child chain finished) ends the task. Same shape as the start menu's selection tasks. */
static BOOL DiagWarpTask(TaskManager *taskman)
{
    static int state;
    if (state == 0) {
        int map, x, z; const char *s = WARP_TO;
        map = (int)strtol(s, (char **)&s, 10); if (*s == ',') s++;
        x = (int)strtol(s, (char **)&s, 10);   if (*s == ',') s++;
        z = (int)strtol(s, (char **)&s, 10);
        VitaNativeMemLog("[DIAG] warp to map %d (%d,%d)", map, x, z);
        printf("[DIAG] warp to map %d (%d,%d)\n", map, x, z);
        state = 1;
        CallTask_ScriptWarp(taskman, (u32)map, -1, x, z, 1);
        return FALSE;
    }
    return TRUE;
}

void __wrap_PlayerAvatar_UpdateMovement(PlayerAvatar *avatar)
{
    static int frames, done;
    if (!done && ++frames == 90) {
        FieldSystem *fieldSystem = MapObject_GetFieldSystem(PlayerAvatar_GetMapObject(avatar));
        done = 1;
        FieldSystem_CreateTask(fieldSystem, DiagWarpTask, NULL);
    }
    __real_PlayerAvatar_UpdateMovement(avatar);
}
