#ifndef VITAPOKE_PLAY
#define VITAPOKE_PLAY 0
#endif
#include <pspkernel.h>
#include <psppower.h>
#include <stdio.h>
#include <stdlib.h>
#include <nitro.h>
#include <nnsys.h>
#include <string.h>
#pragma pack(push,4)
#include "system.h"
#pragma pack(pop)
PSP_MODULE_INFO("Native SS boot proof",0,0,1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(4096);
extern void InitSystemForTheGame(void);
extern void InitGraphicMemory(void);
extern BOOL VitaNativeRomFS_SetPath(const char *path);
extern void WIN_Init_sTexStartAddrTable(void);
extern void WIN_Init_sTexPlttStartAddrTable(void);
/* The original assertion handler. GF_ASSERT is a tail call in most compiled
 * units, so the direct return address is often the assert's own caller; the
 * stack window below names the frames above it. Every line is [FATAL] so a
 * card log on hardware identifies the abort without a debugger. */
void GF_AssertFail(void){
 extern void NitroMain(void);extern char __executable_start[],_etext[];
 extern void VitaNativeMemLog(const char*,...);VitaNativeMemLog("[FATAL] game assertion ra=%p NitroMain=%p",__builtin_return_address(0),NitroMain);
 unsigned *sp=(unsigned *)__builtin_frame_address(0),lo=(unsigned)(uintptr_t)__executable_start,hi=(unsigned)(uintptr_t)_etext,shown=0;
 for(unsigned i=0;i<128&&shown<8;i++){unsigned v=sp[i];if(v>lo&&v<hi&&!(v&3)){VitaNativeMemLog("[FATAL] stack[%u]=%08x",i,v);shown++;}}
 fflush(stdout);abort();}
extern BOOL SSNativeValidateHeader(const u8 *,unsigned,const FSArchive *);
extern const u8 *SSNativeCheckedHeader(void);
static int queueRuns;
static void QueueProof(SysTask *task,void *data){(*(int *)data)++;SysTask_Unlink(task);}
static void VerifyInitialSystems(void){
 const u8 *original=CARD_GetRomHeader();u8 h[512];memcpy(h,original,sizeof(h));
 FSArchive *a=FS_FindArchive("rom",3), bad=*a;int checks=0,failures=0;
 #define CHECK(x) do{checks++;if(!(x)){failures++;printf("[SS-BOOT] check failure line%d\n",__LINE__);}}while(0)
 CHECK(SSNativeValidateHeader(h,512,a));CHECK(!SSNativeValidateHeader(NULL,512,a));CHECK(!SSNativeValidateHeader(h,511,a));
 h[12]^=1;CHECK(!SSNativeValidateHeader(h,512,a));h[12]^=1;
 h[16]^=1;CHECK(!SSNativeValidateHeader(h,512,a));h[16]^=1;
 bad.fat^=8;CHECK(!SSNativeValidateHeader(h,512,&bad));bad=*a;
 bad.fnt^=8;CHECK(!SSNativeValidateHeader(h,512,&bad));bad=*a;
 bad.fat_size^=8;CHECK(!SSNativeValidateHeader(h,512,&bad));bad=*a;
 bad.fnt_size^=8;CHECK(!SSNativeValidateHeader(h,512,&bad));
 CHECK(!memcmp(original,h,512));CHECK(!memcmp(SSNativeCheckedHeader()+12,"ADAJ",4));
 for(int heap=0;heap<4;heap++){if(heap==2)continue;u32 before=HeapExp_FndGetTotalFreeSize(heap);u8 *m=Heap_Alloc(heap,1024);CHECK(m!=NULL);if(m){memset(m,0xa5,1024);CHECK(m[0]==0xa5&&m[1023]==0xa5);Heap_Free(m);}CHECK(HeapExp_FndGetTotalFreeSize(heap)==before);}
 CHECK(gSystem.mainTaskQueue&&gSystem.vblankTaskQueue&&gSystem.vwaitTaskQueue&&gSystem.printTaskQueue);
 SysTask *t=SysTaskQueue_InsertTask(gSystem.mainTaskQueue,QueueProof,&queueRuns,0);CHECK(t!=NULL);SysTaskQueue_RunTasks(gSystem.mainTaskQueue);CHECK(queueRuns==1);SysTaskQueue_RunTasks(gSystem.mainTaskQueue);CHECK(queueRuns==1);
 printf("[SS-BOOT] validation checks=%d failures=%d heap3free=%lu queues=%u/%u/%u/%u\n",checks,failures,(unsigned long)HeapExp_FndGetTotalFreeSize(3),gSystem.mainTaskQueue->limit,gSystem.vblankTaskQueue->limit,gSystem.vwaitTaskQueue->limit,gSystem.printTaskQueue->limit);
 if(failures)abort();
 #undef CHECK
}
/* PSP HOME button: the firmware only shows its quit dialog when an exit callback is registered on a thread that
   sleeps with callbacks enabled (same as the Platinum port). */
static int exitCallback(int a,int b,void*c){sceKernelExitGame();return 0;}
static int callbackThread(SceSize args,void*argp){int cb=sceKernelCreateCallback("exit",exitCallback,NULL);sceKernelRegisterExitCallback(cb);sceKernelSleepThreadCB();return 0;}
/* PSP port: our own EBOOT path, so OS_ResetSystem can relaunch the game (platform.c). */
char gVitaNativeSelfPath[256];
int main(int argc, char **argv){
 if(argc>0&&argv&&argv[0]){strncpy(gVitaNativeSelfPath,argv[0],sizeof(gVitaNativeSelfPath)-1);}
 {int th=sceKernelCreateThread("cbthread",callbackThread,0x11,0xFA0,0,NULL);if(th>=0)sceKernelStartThread(th,0,NULL);}
 scePowerSetClockFrequency(333,333,166);
 if(!VitaNativeRomFS_SetPath("SoulSilver.nds")){puts("[SS-BOOT] invalid ROM path");return 1;}
 extern BOOL VitaNative_OpenBackup(const char *);
 if(!VitaNative_OpenBackup("SoulSilver.native.sav")){extern void VitaNativeMemLog(const char*,...);VitaNativeMemLog("[FATAL] SoulSilver.native.sav missing or unreadable next to the EBOOT");abort();}
 { extern void VitaNativeMemLog(const char*,...); VitaNativeMemLog("[SS-BOOT] native SoulSilver start (play=%d)", (int)VITAPOKE_PLAY); }
#if !VITAPOKE_PLAY
 /* Diagnostic self-proofs (millions of checks): valuable in PPSSPP, seconds of
  * startup on a real PSP. The play build skips them. */
 extern void SSNativeBrightnessProof(void);SSNativeBrightnessProof();
 extern void SSNativeG2dProof(void);SSNativeG2dProof();
 extern void SSNativeFadeProof(void);SSNativeFadeProof();
 extern void SSNativeCullingProof(void);SSNativeCullingProof();
 extern void SSNativeMapMovementProof(void);SSNativeMapMovementProof();
#endif
 CTRDG_Init();
#if !VITAPOKE_PLAY
 extern void SSNativeMenuProof(void);SSNativeMenuProof();
#endif
 extern void VitaNativeFrameInit(void);VitaNativeFrameInit();
 WIN_Init_sTexStartAddrTable();WIN_Init_sTexPlttStartAddrTable();
 extern BOOL VitaNativeOverlay_Init(void);if(!VitaNativeOverlay_Init()){puts("[SS-NITROMAIN] overlay registry initialization failed");abort();}
 puts("[SS-NITROMAIN] entering actual NitroMain; missing game functions abort visibly");
 extern void NitroMain(void);NitroMain();
 sceKernelExitGame();return 0;
}
/* A PSP fatal-error adapter, never a fake successful game operation. */
void PrintErrorMessageAndReset(void){puts("[SS-BOOT] fatal game error/reset");abort();}
/* Actual original routine tests whether its communication manager exists. */
extern void *ssdata_unk_02037C94__021D4150;
BOOL sub_02037D78(void){return ssdata_unk_02037C94__021D4150 != NULL;}

