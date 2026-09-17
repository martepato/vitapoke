#include <psploadexec.h>
#include <string.h>
#include <nitro.h>
#include <pspkernel.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#define arena s_HW_MAIN_MEM
#define ARENA_BYTES (6*1024*1024)
static uintptr_t lo,hi;
void OS_Init(void){OS_InitThread();lo=(uintptr_t)arena;hi=lo+ARENA_BYTES;printf("[STARTUP] OS arena initialized 6MiB in main backing\n");}
void *OS_AllocFromArenaLo(OSArenaId id,u32 size,u32 align){if(id!=OS_ARENA_MAIN||!align||(align&(align-1)))abort();uintptr_t p=(lo+align-1)&~(align-1);if(p+size>hi)return NULL;lo=p+size;return (void*)p;}
void *OS_AllocFromArenaHi(OSArenaId id,u32 size,u32 align){if(id!=OS_ARENA_MAIN||!align||(align&(align-1)))abort();uintptr_t p=(hi-size)&~(align-1);if(size>hi-lo||p<lo)return NULL;hi=p;return (void*)p;}
void OS_GetLowEntropyData(u32*data){u64 t=sceKernelGetSystemTimeWide();for(int i=0;i<8;i++)data[i]=(u32)(t>>(i&1?32:0))^(i*0x9e3779b9u);}

static uint64_t tickStartUs;
static OSTick tickBase;
static BOOL tickReady;
void OS_InitTick(void){tickStartUs=sceKernelGetSystemTimeWide();tickBase=0;tickReady=TRUE;}
BOOL OS_IsTickAvailable(void){return tickReady;}
OSTick OS_GetTick(void){
 uint64_t elapsed=sceKernelGetSystemTimeWide()-tickStartUs;
 // DS OS ticks use systemclock/64, not PSP microseconds. Split the quotient
 // to avoid multiplying a long-running microsecond count past64bits.
 return tickBase+(elapsed/64000000u)*OS_SYSTEM_CLOCK+((elapsed%64000000u)*OS_SYSTEM_CLOCK)/64000000u;
}
u16 OS_GetTickLo(void){return (u16)OS_GetTick();}
void OS_SetTick(OSTick value){tickStartUs=sceKernelGetSystemTimeWide();tickBase=value;tickReady=TRUE;}

static OSIntrMode intr;
OSIntrMode OS_DisableInterrupts(void){OSIntrMode old=intr;intr=1;return old;}
OSIntrMode OS_RestoreInterrupts(OSIntrMode mode){OSIntrMode old=intr;intr=mode;return old;}
static OSProcMode procMode = OS_PROCMODE_SYS;
extern BOOL VitaNativeAlarmInCallback(void);
OSProcMode OS_GetProcMode(void){return VitaNativeAlarmInCallback()?OS_PROCMODE_IRQ:procMode;}
static OSIrqFunction callbacks[32];static OSIrqMask mask;
void OS_SetIrqFunction(OSIrqMask bits,OSIrqFunction fn){for(int i=0;i<32;i++)if(bits&(1u<<i))callbacks[i]=fn;}
OSIrqMask OS_EnableIrqMask(OSIrqMask bits){OSIrqMask old=mask;mask|=bits;return old;}
void SIM_handleAssertionFailureMsg(const char*file,unsigned line,const char*fmt,...){extern void VitaNativeMemLog(const char*,...);char m[200];va_list a;va_start(a,fmt);vsnprintf(m,sizeof m,fmt,a);va_end(a);VitaNativeMemLog("[FATAL] SDK assertion %s:%u %s",file,line,m);fflush(stdout);abort();}
void ErrorHandling_AssertFail(void){printf("[STARTUP] game assertion\n");abort();}
void OS_Terminate(void){printf("[STARTUP] OS terminate\n");abort();}


static u32 cardIcacheThreshold,cardDcacheThreshold;
void CARD_SetCacheFlushThreshold(u32 icache,u32 dcache){cardIcacheThreshold=icache;cardDcacheThreshold=dcache;printf("[STARTUP] CARD cache thresholds %lu/%lu; synchronous stdio backend\n",(unsigned long)icache,(unsigned long)dcache);}

void OS_WaitIrq(BOOL clear,OSIrqMask bits){
 if(bits!=OS_IE_V_BLANK){printf("[STARTUP] unsupported IRQ wait %08lx\n",(unsigned long)bits);abort();}
 if(clear)s_HW_INTR_CHECK_BUF &= ~bits;
 extern void VitaNativeVBlankWait(void);VitaNativeVBlankWait();
 extern void VitaNativeInputVBlank(void);VitaNativeInputVBlank();
 if(!intr && (mask & bits) && callbacks[0]){OSProcMode old=procMode;procMode=OS_PROCMODE_IRQ;callbacks[0]();procMode=old;}
}

/* Native PSP cache maintenance: preserves the SDK visibility contract for GE. */
OSIrqMask OS_DisableIrqMask(OSIrqMask bits){OSIrqMask old=mask;mask&=~bits;return old;}
OSIrqMask OS_SetIrqMask(OSIrqMask bits){OSIrqMask old=mask;mask=bits;return old;}
OSIrqMask OS_ResetRequestIrqMask(OSIrqMask bits){OSIrqMask old=reg_OS_IF;reg_OS_IF&=~bits;return old;}
OSIrqFunction OS_GetIrqFunction(OSIrqMask bits){for(int i=0;i<32;i++)if(bits&(1u<<i))return callbacks[i];return NULL;}
void *OS_GetInitArenaLo(OSArenaId id){if(id!=OS_ARENA_MAIN)abort();return arena;}
void *OS_GetInitArenaHi(OSArenaId id){if(id!=OS_ARENA_MAIN)abort();return arena+ARENA_BYTES;}
void OS_SetArenaLo(OSArenaId id,void*p){if(id!=OS_ARENA_MAIN||(uintptr_t)p<(uintptr_t)arena||(uintptr_t)p>hi)abort();lo=(uintptr_t)p;}
void OS_SetArenaHi(OSArenaId id,void*p){if(id!=OS_ARENA_MAIN||(uintptr_t)p<lo||(uintptr_t)p>(uintptr_t)arena+ARENA_BYTES)abort();hi=(uintptr_t)p;}

void OS_WaitVBlankIntr(void){OS_WaitIrq(TRUE,OS_IE_V_BLANK);}

void SIM_handleAssertionFailure(const char*file,unsigned line,const char*expr){printf("[NATIVE] assertion %s:%u %s\n",file,line,expr);abort();}
void OS_IrqHandler(void){puts("[NATIVE] unsupported direct DS network IRQ handler");abort();}
/* PSP port: the DS soft-resets to the title screen (after the Hall of Fame credits, soft-reset combo, errors).
 * Relaunch our own EBOOT; the save has already been written synchronously by then. */
extern char gVitaNativeSelfPath[256];
void OS_ResetSystem(u32 parameter){extern void VitaNativeMemLog(const char*,...);VitaNativeMemLog("[NATIVE] reset requested %lu; relaunching %s",(unsigned long)parameter,gVitaNativeSelfPath);printf("[NATIVE] reset requested %lu; relaunching %s\n",(unsigned long)parameter,gVitaNativeSelfPath);
 if(gVitaNativeSelfPath[0]){struct SceKernelLoadExecParam prm;prm.size=sizeof(prm);prm.args=strlen(gVitaNativeSelfPath)+1;prm.argp=gVitaNativeSelfPath;prm.key=NULL;int r=sceKernelLoadExec(gVitaNativeSelfPath,&prm);printf("[NATIVE] sceKernelLoadExec returned %d\n",r);VitaNativeMemLog("[NATIVE] sceKernelLoadExec returned %d",r);}
 printf("[NATIVE] native restart failed\n");abort();}
void OS_SetDPermissionsForProtectionRegion(u32 mask,u32 flags){printf("[NATIVE] unsupported DS cartridge protection %lx/%lx\n",(unsigned long)mask,(unsigned long)flags);abort();}
void SVC_WaitByLoop(s32 count){if(count>0){uint64_t us=((uint64_t)(u32)count*4000000u+OS_SYSTEM_CLOCK-1)/OS_SYSTEM_CLOCK;while(us){u32 part=us>1000000?1000000:(u32)us;sceKernelDelayThread(part);us-=part;}}}

/* PSP port: translated DS code writes the SDK's VBlank "IRQ check flag" inline as
 * OS_IRQTable[0x3FF8/4] |= 1 (e.g. the evolution scene's VBlank callback sub_02077270).
 * This port's OS_WaitVBlankIntr/OS_WaitIrq are simulated and never read that flag, so a
 * zero-filled table of the DS size (0x4000 bytes, covering offset 0x3FF8) only has to
 * absorb the write. */
OSIrqFunction OS_IRQTable[0x1000];
/* The renderer computes the clip matrix lazily (perf port 2026-09-13). The only reader of the
   CLIPMTX_RESULT registers is G3X_GetClipMtx, wrapped so a read always sees the current matrix. */
extern void G3SIM_EnsureClipMatrix(void);
extern int __real_G3X_GetClipMtx(void *m);
int __wrap_G3X_GetClipMtx(void *m){G3SIM_EnsureClipMatrix();return __real_G3X_GetClipMtx(m);}
