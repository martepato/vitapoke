/* VITAPOKE_GAME_PROF: coarse game-thread CPU buckets, one card-log line per 600 frames.
   Link-time wraps (Makefile GAME_PROF=1) around a handful of per-frame subsystem entry points.
   Each wrapped call costs two sceKernelGetProcessTimeLow syscalls; ~15 calls per field frame.
   Nested 3D command-renderer time (g3ProfUs, sampled 1/8 by the renderer) is subtracted so each
   bucket reports game-side CPU only ("self_us" = inclusive - g3 inside it).
   malloc/free/memalign are counted on every call; one call in 64 is timed and scaled. */
#include <psp2/kernel/processmgr.h>
#include <stddef.h>
#include "system.h"
extern unsigned g3ProfUs;
extern void VitaNativeMemLog(const char*,...);
enum{B_APP,B_TASK_MAIN,B_TASK_PRINT,B_TASK_POSTVB,B_LAND,B_PROPS2,B_FIELDEFF,B_OWANIM,B_BILLBOARD,B_SOUND,B_SWAP,B_KEYPAD,B_COMM,B_COUNT};
static const char*const kNames[B_COUNT]={"app","task_main","task_print","task_postvb","land_render","props_render2","fieldeff_render","owanim_render","billboard","sound_tick","swap","keypad","comm"};
static struct{unsigned long long us,g3;unsigned calls;}b[B_COUNT];
static unsigned mallocN,freeN,memalignN,reallocN,callocN,allocTimedN;static unsigned long long allocTimedUs;
#define BEGIN unsigned t0=sceKernelGetProcessTimeLow(),g0=g3ProfUs
#define END(id) {unsigned t1=sceKernelGetProcessTimeLow();b[id].us+=t1-t0;b[id].g3+=g3ProfUs-g0;b[id].calls++;}
#define WRAP_V1(id,name,T1) extern void __real_##name(T1);void __wrap_##name(T1 a){BEGIN;__real_##name(a);END(id)}
#define WRAP_V2(id,name,T1,T2) extern void __real_##name(T1,T2);void __wrap_##name(T1 a,T2 c){BEGIN;__real_##name(a,c);END(id)}
#define WRAP_V0(id,name) extern void __real_##name(void);void __wrap_##name(void){BEGIN;__real_##name();END(id)}
extern BOOL __real_ApplicationManager_Exec(void*);BOOL __wrap_ApplicationManager_Exec(void*m){BEGIN;BOOL r=__real_ApplicationManager_Exec(m);END(B_APP)return r;}
extern BOOL __real_CommSys_Update(void);BOOL __wrap_CommSys_Update(void){BEGIN;BOOL r=__real_CommSys_Update();END(B_COMM)return r;}
extern void __real_SysTaskManager_ExecuteTasks(SysTaskManager*);
void __wrap_SysTaskManager_ExecuteTasks(SysTaskManager*m){BEGIN;__real_SysTaskManager_ExecuteTasks(m);int id=m==gSystem.mainTaskMgr?B_TASK_MAIN:m==gSystem.printTaskMgr?B_TASK_PRINT:B_TASK_POSTVB;END(id)}
WRAP_V2(B_LAND,LandDataManager_RenderLoadedMaps,void*,void*)
WRAP_V2(B_PROPS2,MapPropManager_Render2,void*,void*)
WRAP_V1(B_FIELDEFF,FieldEffectManager_Render,void*)
WRAP_V1(B_OWANIM,OverworldAnimManagerList_Render,void*)
WRAP_V0(B_BILLBOARD,BillboardLists_Draw)
WRAP_V0(B_SOUND,SoundSystem_Tick)
WRAP_V0(B_SWAP,sub_020241CC)
WRAP_V0(B_KEYPAD,ReadKeypadAndTouchpad)
/* allocator: count always, time 1/64 */
extern void*__real_malloc(size_t);extern void __real_free(void*);extern void*__real_memalign(size_t,size_t);extern void*__real_realloc(void*,size_t);extern void*__real_calloc(size_t,size_t);
#define ATIME(expr) if(((mallocN+freeN)&63)==0){unsigned t=sceKernelGetProcessTimeLow();expr;allocTimedUs+=sceKernelGetProcessTimeLow()-t;allocTimedN++;}else{expr;}
void*__wrap_malloc(size_t n){void*p;mallocN++;ATIME(p=__real_malloc(n))return p;}
void __wrap_free(void*p){freeN++;ATIME(__real_free(p))}
void*__wrap_memalign(size_t a,size_t n){memalignN++;return __real_memalign(a,n);}
void*__wrap_realloc(void*p,size_t n){reallocN++;return __real_realloc(p,n);}
void*__wrap_calloc(size_t a,size_t n){callocN++;return __real_calloc(a,n);}
void VitaNativeGameProfLine(unsigned frames){
 if(!frames)return;
 char line[1024];int o=0;
 o+=snprintf(line+o,sizeof line-o,"[GPROF] frames=%u",frames);
 for(int i=0;i<B_COUNT;i++){long long self=(long long)b[i].us-(long long)b[i].g3;o+=snprintf(line+o,sizeof line-o," %s=%lld/%llu/%u",kNames[i],self/(long long)frames,b[i].g3/frames,b[i].calls/frames);if(o>=(int)sizeof line)break;}
 VitaNativeMemLog("%s",line);
 unsigned ops=mallocN+freeN;
 VitaNativeMemLog("[GPROF-ALLOC] frames=%u malloc=%u free=%u memalign=%u realloc=%u calloc=%u est_us=%llu timed_n=%u (buckets: self_us/g3_us/calls per frame)",frames,mallocN/frames,freeN/frames,memalignN/frames,reallocN/frames,callocN/frames,allocTimedN?(allocTimedUs*(unsigned long long)ops/allocTimedN)/frames:0ULL,allocTimedN);
 for(int i=0;i<B_COUNT;i++){b[i].us=b[i].g3=0;b[i].calls=0;}
 mallocN=freeN=memalignN=reallocN=callocN=allocTimedN=0;allocTimedUs=0;
}
