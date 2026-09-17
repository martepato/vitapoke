/* PSP port: Nintendo Wi-Fi Connection is not available. ScrCmd_166 (Global Terminal) and ScrCmd_152 start the connection
 * task sub_02078834, which - when the save has no Wi-Fi profile - launches the WFC setup app (overlay 44), and that app
 * loads the wireless SDK overlay 0 that the port cannot host, so the game exited. Without a profile, finish the task
 * immediately with result 0 (the value the original uses when it launches nothing); the scripts then take their
 * "cannot connect" branch. ScrCmd_152 scripts already run ScrCmd_436 (leave overworld) before the command. With a profile (not creatable on PSP) the original task runs unchanged. */
#include <nitro.h>

typedef struct TaskManager TaskManager;
extern void *TaskManager_GetFieldSystem(TaskManager *taskManager);
extern void TaskManager_Call(TaskManager *taskManager, BOOL (*func)(TaskManager *), void *env);
extern BOOL sub_0203A05C(void *saveData);
extern void CallTask_LeaveOverworld(TaskManager *taskManager);
extern void *FieldSystem_GetSaveData(void *fieldSystem);
extern void __real_sub_02078B78(TaskManager *taskManager, u16 *var_p);
extern void __real_sub_02078B58(TaskManager *taskManager);
extern void VitaNativeMemLog(const char *fmt, ...);

static u16 *sResult;

static BOOL WifiUnavailableTask(TaskManager *taskManager)
{
    (void)taskManager;
    if (sResult != NULL) {
        *sResult = 0;
        sResult = NULL;
    }
    return TRUE;
}

void __wrap_sub_02078B78(TaskManager *taskManager, u16 *var_p)
{
    if (sub_0203A05C(FieldSystem_GetSaveData(TaskManager_GetFieldSystem(taskManager)))) {
        __real_sub_02078B78(taskManager, var_p);
        return;
    }
    /* ScrCmd_166 scripts (Global Terminal) do not leave the overworld themselves - the skipped setup app did - and they
     * end with RestoreOverworld, which asserts if the overworld is still up. Leave it the way the app launch would. */
    VitaNativeMemLog("[SS-WIFI] no Wi-Fi profile: connection task skipped, overworld left (ScrCmd_166)");
    if (var_p != NULL) {
        *var_p = 0;
    }
    CallTask_LeaveOverworld(taskManager);
}

void __wrap_sub_02078B58(TaskManager *taskManager)
{
    if (sub_0203A05C(FieldSystem_GetSaveData(TaskManager_GetFieldSystem(taskManager)))) {
        __real_sub_02078B58(taskManager);
        return;
    }
    VitaNativeMemLog("[SS-WIFI] no Wi-Fi profile: connection task skipped (ScrCmd_152)");
    sResult = NULL;
    TaskManager_Call(taskManager, WifiUnavailableTask, NULL);
}
