#include <nitro.h>
#include <pspkernel.h>
#include <stdio.h>
#include "system.h"
#include "heap.h"
#include "rtc.h"
#include <pspctrl.h>
#include <psprtc.h>
#include "sys_task_manager.h"
#include <stdint.h>
#include <string.h>
PSP_MODULE_INFO("Native Startup",0,1,0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);

static unsigned ran,failures;
static void TaskCheck(SysTask*t,void*p){if(OS_GetProcMode()!=OS_PROCMODE_IRQ)failures++;ran++;SysTask_Delete(t);}
static unsigned CheckWords(const u32*p,unsigned n,u32 value){unsigned bad=0;for(unsigned i=0;i<n;i++)bad+=p[i]!=value;return bad;}
int main(void){
 printf("[STARTUP] enter InitSystem\n");InitSystem();printf("[STARTUP] InitSystem returned\n");
 memset((void*)(uintptr_t)HW_LCDC_VRAM,0xA5,HW_LCDC_VRAM_SIZE);
 memset((void*)(uintptr_t)HW_PLTT,0xA5,HW_PLTT_SIZE);memset((void*)(uintptr_t)HW_DB_PLTT,0xA5,HW_DB_PLTT_SIZE);
 InitVRAM();printf("[STARTUP] InitVRAM returned\n");
 failures+=CheckWords((void*)(uintptr_t)HW_LCDC_VRAM,HW_LCDC_VRAM_SIZE/4,0);
 failures+=CheckWords((void*)(uintptr_t)HW_PLTT,HW_PLTT_SIZE/4,0)+CheckWords((void*)(uintptr_t)HW_DB_PLTT,HW_DB_PLTT_SIZE/4,0);
 failures+=CheckWords((void*)(uintptr_t)HW_OAM,HW_OAM_SIZE/4,192)+CheckWords((void*)(uintptr_t)HW_DB_OAM,HW_DB_OAM_SIZE/4,192);
 if(!gSystem.mainTaskMgr||!gSystem.vBlankTaskMgr||!gSystem.postVBlankTaskMgr||!gSystem.printTaskMgr)abort();
 if(gSystem.mainTaskMgr->maxTasks!=160||gSystem.vBlankTaskMgr->maxTasks!=64||gSystem.postVBlankTaskMgr->maxTasks!=32||gSystem.printTaskMgr->maxTasks!=4)failures++;
 for(unsigned id=0;id<4;id++){if(id==HEAP_ID_DEBUG)continue;u32 before=HeapExp_FndGetTotalFreeSize(id);if(id!=HEAP_ID_DEBUG){void*p=Heap_Alloc(id,128);if(!p)abort();memset(p,0x5A,128);Heap_Free(p);if(HeapExp_FndGetTotalFreeSize(id)!=before)failures++;}printf("[STARTUP] heap%u free=%lu\n",id,(unsigned long)before);}
 FSFile f;FS_InitFile(&f);char magic[4];if(!FS_OpenFile(&f,"graphic/pl_font.narc")||FS_ReadFile(&f,magic,4)!=4||memcmp(magic,"NARC",4))failures++;FS_CloseFile(&f);
 SysTaskManager_AddTask(gSystem.vBlankTaskMgr,TaskCheck,NULL,1);unsigned previous=gSystem.frameCounter;OS_WaitIrq(TRUE,OS_IE_V_BLANK);
 if(ran!=1||gSystem.frameCounter!=previous+1||OS_GetProcMode()!=OS_PROCMODE_SYS)failures++;
 extern void VitaNativeInputStep(unsigned,unsigned,unsigned);extern void VitaNativeInputPoll(void);
 InitKeypadAndTouchpad();
 const unsigned map[12]={PSP_CTRL_CIRCLE,PSP_CTRL_CROSS,PSP_CTRL_SELECT,PSP_CTRL_START,PSP_CTRL_RIGHT,PSP_CTRL_LEFT,PSP_CTRL_UP,PSP_CTRL_DOWN,PSP_CTRL_RTRIGGER,PSP_CTRL_LTRIGGER,PSP_CTRL_TRIANGLE,PSP_CTRL_SQUARE};
 for(unsigned i=0;i<12;i++){VitaNativeInputStep(map[i],128,128);ReadKeypadAndTouchpad();if(gSystem.heldKeys!=(1u<<i)||gSystem.pressedKeys!=(1u<<i))failures++;VitaNativeInputStep(0,128,128);ReadKeypadAndTouchpad();}

 VitaNativeInputStep(PSP_CTRL_CIRCLE,128,128);ReadKeypadAndTouchpad();if(gSystem.heldKeys!=PAD_BUTTON_A||gSystem.pressedKeys!=PAD_BUTTON_A)failures++;
 for(unsigned i=1;i<=8;i++){VitaNativeInputStep(PSP_CTRL_CIRCLE,128,128);ReadKeypadAndTouchpad();if(gSystem.pressedKeys||((i==8)!=(gSystem.pressedKeysRepeatable==PAD_BUTTON_A)))failures++;}
 VitaNativeInputStep(0,128,128);ReadKeypadAndTouchpad();if(gSystem.heldKeys||gSystem.touchHeld)failures++;
 VitaNativeInputStep(PSP_CTRL_SELECT|PSP_CTRL_CROSS,128,128);ReadKeypadAndTouchpad();if(!gSystem.touchPressed||!gSystem.touchHeld||(gSystem.heldKeys&PAD_BUTTON_B))failures++;
 VitaNativeInputStep(PSP_CTRL_CROSS|PSP_CTRL_RIGHT,255,128);ReadKeypadAndTouchpad();if(gSystem.touchPressed||gSystem.touchX!=133||!gSystem.touchHeld||(gSystem.heldKeys&PAD_PLUS_KEY_MASK))failures++;
 VitaNativeInputStep(0,128,128);ReadKeypadAndTouchpad();if(gSystem.touchHeld)failures++;
 VitaNativeInputStep(PSP_CTRL_SELECT|PSP_CTRL_CROSS,128,128);ReadKeypadAndTouchpad();if(gSystem.touchHeld||!(gSystem.heldKeys&PAD_BUTTON_B))failures++;
 for(unsigned i=0;i<200;i++)VitaNativeInputStep(0,0,0);ReadKeypadAndTouchpad();if(gSystem.touchX||gSystem.touchY)failures++;
 VitaNativeInputStep(PSP_CTRL_TRIANGLE|PSP_CTRL_SQUARE|PSP_CTRL_START,128,128);ReadKeypadAndTouchpad();if(gSystem.heldKeys!=(PAD_BUTTON_X|PAD_BUTTON_Y|PAD_BUTTON_START))failures++;
 VitaNativeInputPoll();ReadKeypadAndTouchpad();
 InitRTC();RTCDate date;RTCTime time;GetCurrentDateTime(&date,&time);ScePspDateTime now;sceRtcGetCurrentClockLocalTime(&now);
 if(date.year+2000!=now.year||date.month!=now.month||date.day!=now.day||time.hour!=now.hour||time.minute!=now.minute)failures++;
 extern unsigned VitaNativeRTCReadCount(void);unsigned reads=VitaNativeRTCReadCount();s64 stamp=GetTimestamp();for(unsigned i=0;i<11;i++)UpdateRTC();if(GetTimestamp()<stamp||VitaNativeRTCReadCount()!=reads+1)failures++;
 printf("[INPUT-RTC] failures=%u timestamp=%lld date=%lu-%lu-%lu %lu:%lu:%lu\n",failures,stamp,(unsigned long)date.year+2000,(unsigned long)date.month,(unsigned long)date.day,(unsigned long)time.hour,(unsigned long)time.minute,(unsigned long)time.second);
 printf("[STARTUP] validation failures=%u VBlankTasks=%u frame=%u FS=%d\n",failures,ran,gSystem.frameCounter,FS_IsAvailable());sceKernelExitGame();return failures;
}
