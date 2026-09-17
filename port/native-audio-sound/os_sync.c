#include <nitro.h>
#include <pspkernel.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef struct {SceUID gate,readable,writable;} Queue;
typedef struct {SceUID gate,owner;unsigned depth;} Mutex;
static SceUID NewSema(const char*name,int initial,int maximum){SceUID id=sceKernelCreateSema(name,0,initial,maximum,NULL);if(id<0)abort();return id;}
static int Take(SceUID id,int flags){int code=flags&OS_MESSAGE_BLOCK?sceKernelWaitSema(id,1,NULL):sceKernelPollSema(id,1);return code>=0;}
static void Give(SceUID id){if(sceKernelSignalSema(id,1)<0)abort();}
void OS_InitMessageQueue(OSMessageQueue*q,OSMessage*array,s32 capacity){if(!q||!array||capacity<=0)abort();memset(q,0,sizeof(*q));Queue*r=malloc(sizeof(*r));if(!r)abort();r->gate=NewSema("NitroQGate",1,1);r->readable=NewSema("NitroQRead",0,capacity);r->writable=NewSema("NitroQWrite",capacity,capacity);q->msgArray=array;q->msgCount=capacity;q->semaphore=(SDL_sem*)r;}
static BOOL Send(OSMessageQueue*q,OSMessage msg,s32 flags,BOOL front){Queue*r=(Queue*)q->semaphore;if(!r)abort();if(!Take(r->writable,flags))return FALSE;if(!Take(r->gate,OS_MESSAGE_BLOCK))abort();unsigned pos;if(front){q->firstIndex=(q->firstIndex+q->msgCount-1)%q->msgCount;pos=q->firstIndex;}else pos=(q->firstIndex+q->usedCount)%q->msgCount;q->msgArray[pos]=msg;q->usedCount++;Give(r->gate);Give(r->readable);return TRUE;}
BOOL OS_SendMessage(OSMessageQueue*q,OSMessage msg,s32 flags){return Send(q,msg,flags,FALSE);}
BOOL OS_JamMessage(OSMessageQueue*q,OSMessage msg,s32 flags){return Send(q,msg,flags,TRUE);}
static BOOL Receive(OSMessageQueue*q,OSMessage*out,s32 flags,BOOL peek){Queue*r=(Queue*)q->semaphore;if(!r)abort();if(!Take(r->readable,flags))return FALSE;if(!Take(r->gate,OS_MESSAGE_BLOCK))abort();if(out)*out=q->msgArray[q->firstIndex];if(!peek){q->firstIndex=(q->firstIndex+1)%q->msgCount;q->usedCount--;}Give(r->gate);Give(peek?r->readable:r->writable);return TRUE;}
BOOL OS_ReceiveMessage(OSMessageQueue*q,OSMessage*out,s32 flags){return Receive(q,out,flags,FALSE);}
BOOL OS_ReadMessage(OSMessageQueue*q,OSMessage*out,s32 flags){return Receive(q,out,flags,TRUE);}
void OS_InitMutex(OSMutex*m){if(!m)abort();memset(m,0,sizeof(*m));Mutex*r=calloc(1,sizeof(*r));if(!r)abort();r->gate=NewSema("NitroMutex",1,1);r->owner=-1;m->sdlMutex=(SDL_mutex*)r;}
BOOL OS_TryLockMutex(OSMutex*m){Mutex*r=(Mutex*)m->sdlMutex;SceUID me=sceKernelGetThreadId();if(r->owner!=me){if(!Take(r->gate,0))return FALSE;r->owner=me;}r->depth++;m->count=r->depth;return TRUE;}
void OS_LockMutex(OSMutex*m){Mutex*r=(Mutex*)m->sdlMutex;SceUID me=sceKernelGetThreadId();if(r->owner!=me){if(!Take(r->gate,1))abort();r->owner=me;}r->depth++;m->count=r->depth;}
void OS_UnlockMutex(OSMutex*m){Mutex*r=(Mutex*)m->sdlMutex;if(r->owner!=sceKernelGetThreadId()||!r->depth)abort();m->count=--r->depth;if(!r->depth){r->owner=-1;Give(r->gate);}}
void DC_FlushRange(const void*p,u32 n){if(n)sceKernelDcacheWritebackInvalidateRange(p,n);}
void DC_StoreRange(const void*p,u32 n){if(n)sceKernelDcacheWritebackRange(p,n);}
void DC_InvalidateRange(void*p,u32 n){if(n)sceKernelDcacheInvalidateRange(p,n);}
void DC_FlushAll(void){sceKernelDcacheWritebackInvalidateAll();}
/* The DS drains the ARM9 write buffer here so a store is visible to the hardware that follows it.
 * MIPS spells that barrier `sync`; on ARM it is a data memory barrier. `dmb ish` is the inner-shareable
 * domain, which covers the other cores and the GPU's view of memory the port writes. */
#if defined(__arm__)
void DC_WaitWriteBufferEmpty(void){__asm__ volatile("dmb ish":: :"memory");}
#else
void DC_WaitWriteBufferEmpty(void){__asm__ volatile("sync":: :"memory");}
#endif
// This flag means the Nitro emulator's private 0x04FFF200 interface, which PSP lacks.
BOOL OS_IsRunOnEmulator(void){return FALSE;}
// Yield instead of burning host cycles; sound command completion must be serviced
// by the native backend, never fabricated by this delay.
void OS_SpinWait(u32 cycles){u64 us=((u64)cycles*1000000u+OS_SYSTEM_CLOCK-1)/OS_SYSTEM_CLOCK;if(us)sceKernelDelayThread((u32)us);}
