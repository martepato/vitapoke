/* vitapoke: the DS SDK's core OS services on the Vita.
 *
 * This implements the DS interfaces the game and libntr call -- the arena allocator, the tick clock,
 * the interrupt model and the failure paths -- directly on psp2. It is the Vita counterpart of
 * port/native-audio-app/platform.c, not a translation of it: where the PSP build could lean on that
 * console having one core and a maskable interrupt line, this uses the DS execution lock described in
 * include/vita_os.h.
 */
/* stdlib.h before nitro.h: libntr's nitro/card/backup.h calls malloc and free without declaring
 * them, and relies on the including translation unit having done it. The PSP build gets that from
 * its precompiled header. */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include <nitro.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/rng.h>

#include "include/vita_os.h"
#include "include/vita_input.h"

/* ---------------------------------------------------------------- the DS execution lock */

static SceKernelLwMutexWork executionLock;
static SceKernelLwMutexWork tableLock;
static int locksReady;
static SceUID lockOwner = -1;
static unsigned lockDepth;

static void LocksInit(void)
{
	if (locksReady)
		return;
	/* Recursive: DS code routinely calls one OS_ function from inside another. */
	sceKernelCreateLwMutex(&executionLock, "vitapoke_ds", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, NULL);
	sceKernelCreateLwMutex(&tableLock, "vitapoke_tables", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, NULL);
	locksReady = 1;
}

void VitaOS_Enter(void)
{
	LocksInit();
	sceKernelLockLwMutex(&executionLock, 1, NULL);
	lockOwner = sceKernelGetThreadId();
	lockDepth++;
}

void VitaOS_Leave(void)
{
	/* Only unlock if this thread actually holds it. An unbalanced leave would otherwise unlock a
	 * mutex owned by somebody else, which psp2 rejects and which would let two DS threads into DS
	 * code at once -- the exact thing the lock exists to prevent, failing silently. */
	if (!VitaOS_Held())
		return;
	if (--lockDepth == 0)
		lockOwner = -1;
	sceKernelUnlockLwMutex(&executionLock, 1);
}

int VitaOS_Held(void)
{
	return locksReady && lockDepth > 0 && lockOwner == sceKernelGetThreadId();
}

unsigned VitaOS_Release(void)
{
	unsigned depth, i;

	if (!VitaOS_Held())
		return 0;
	/* Drop every level this thread holds, so a nested DS call that blocks does not keep the lock
	 * and stall every other DS thread. The depth comes back on reacquire. */
	depth = lockDepth;
	lockDepth = 0;
	lockOwner = -1;
	for (i = 0; i < depth; i++)
		sceKernelUnlockLwMutex(&executionLock, 1);
	return depth;
}

void VitaOS_Reacquire(unsigned depth)
{
	while (depth-- > 0)
		VitaOS_Enter();
}

void VitaOS_TableLock(void)
{
	LocksInit();
	sceKernelLockLwMutex(&tableLock, 1, NULL);
}

void VitaOS_TableUnlock(void) { sceKernelUnlockLwMutex(&tableLock, 1); }

unsigned long long VitaOS_Now(void) { return sceKernelGetProcessTimeWide(); }

/* ---------------------------------------------------------------- the arena
 *
 * The DS SDK hands the game one block of main memory to sub-allocate from both ends. On the DS this is
 * real RAM at a fixed address; here it is a static array inside the application, exactly as on the PSP,
 * because the game's own allocator runs out of it and expects the addresses to be stable.
 *
 * 6 MiB is what the DS gives the game and what the PSP build reserves. The Vita has memory to spare and
 * could be more generous, but the size is part of the game's behaviour -- it decides when the game's
 * allocator reports out of memory -- so it stays as the DS has it.
 */
#define ARENA_BYTES (6 * 1024 * 1024)
#define arena s_HW_MAIN_MEM

static uintptr_t arenaLo, arenaHi;

void OS_Init(void)
{
	LocksInit();
	OS_InitThread();
	arenaLo = (uintptr_t)arena;
	arenaHi = arenaLo + ARENA_BYTES;
	PSPNativeMemLog("[STARTUP] OS arena initialised, 6 MiB in main backing");
}

void *OS_AllocFromArenaLo(OSArenaId id, u32 size, u32 align)
{
	uintptr_t p;
	if (id != OS_ARENA_MAIN || !align || (align & (align - 1)))
		PSPNativeFatal("OS_AllocFromArenaLo: bad arena or alignment");
	p = (arenaLo + align - 1) & ~(uintptr_t)(align - 1);
	if (p + size > arenaHi)
		return NULL;
	arenaLo = p + size;
	return (void *)p;
}

void *OS_AllocFromArenaHi(OSArenaId id, u32 size, u32 align)
{
	uintptr_t p;
	if (id != OS_ARENA_MAIN || !align || (align & (align - 1)))
		PSPNativeFatal("OS_AllocFromArenaHi: bad arena or alignment");
	p = (arenaHi - size) & ~(uintptr_t)(align - 1);
	if (size > arenaHi - arenaLo || p < arenaLo)
		return NULL;
	arenaHi = p;
	return (void *)p;
}

void *OS_GetInitArenaLo(OSArenaId id)
{
	if (id != OS_ARENA_MAIN)
		PSPNativeFatal("OS_GetInitArenaLo: bad arena");
	return arena;
}

void *OS_GetInitArenaHi(OSArenaId id)
{
	if (id != OS_ARENA_MAIN)
		PSPNativeFatal("OS_GetInitArenaHi: bad arena");
	return arena + ARENA_BYTES;
}

void OS_SetArenaLo(OSArenaId id, void *p)
{
	if (id != OS_ARENA_MAIN || (uintptr_t)p < (uintptr_t)arena || (uintptr_t)p > arenaHi)
		PSPNativeFatal("OS_SetArenaLo: out of range");
	arenaLo = (uintptr_t)p;
}

void OS_SetArenaHi(OSArenaId id, void *p)
{
	if (id != OS_ARENA_MAIN || (uintptr_t)p < arenaLo ||
	    (uintptr_t)p > (uintptr_t)arena + ARENA_BYTES)
		PSPNativeFatal("OS_SetArenaHi: out of range");
	arenaHi = (uintptr_t)p;
}

/* ---------------------------------------------------------------- the tick clock
 *
 * DS ticks are the system clock divided by 64, not microseconds, so every reading is converted. The
 * quotient is split before scaling because a microsecond count that has been running for hours would
 * otherwise overflow when multiplied by the clock.
 */
static unsigned long long tickStartUs;
static OSTick tickBase;
static BOOL tickReady;

void OS_InitTick(void)
{
	tickStartUs = VitaOS_Now();
	tickBase = 0;
	tickReady = TRUE;
}

BOOL OS_IsTickAvailable(void) { return tickReady; }

OSTick OS_GetTick(void)
{
	unsigned long long elapsed = VitaOS_Now() - tickStartUs;
	return tickBase + (elapsed / 64000000u) * OS_SYSTEM_CLOCK +
	       ((elapsed % 64000000u) * OS_SYSTEM_CLOCK) / 64000000u;
}

u16 OS_GetTickLo(void) { return (u16)OS_GetTick(); }

void OS_SetTick(OSTick value)
{
	tickStartUs = VitaOS_Now();
	tickBase = value;
	tickReady = TRUE;
}

/* The DS spins for a count of cycles. Sleeping for the equivalent time gives the other DS threads a
 * turn, which a spin on one of four cores would not. */
void SVC_WaitByLoop(s32 count)
{
	unsigned long long us;
	if (count <= 0)
		return;
	us = ((unsigned long long)(u32)count * 4000000u + OS_SYSTEM_CLOCK - 1) / OS_SYSTEM_CLOCK;
	while (us) {
		unsigned part = us > 1000000 ? 1000000 : (unsigned)us;
		sceKernelDelayThread(part);
		us -= part;
	}
}

/* ---------------------------------------------------------------- entropy
 *
 * The game seeds its random number generator from this. The DS mixes uninitialised hardware state; the
 * Vita has a real random number generator, so use it and fall back to the clock only if it refuses.
 */
void OS_GetLowEntropyData(u32 *data)
{
	if (sceKernelGetRandomNumber(data, 8 * sizeof(u32)) >= 0)
		return;
	{
		unsigned long long t = VitaOS_Now();
		for (int i = 0; i < 8; i++)
			data[i] = (u32)(t >> (i & 1 ? 32 : 0)) ^ (u32)(i * 0x9e3779b9u);
	}
}

/* ---------------------------------------------------------------- interrupts
 *
 * The DS's interrupt enable flag is state the game reads and writes, so it is kept as the DS sees it.
 * What it does *not* do here is stop other threads: on the DS that was a side effect of there being one
 * core, and on the Vita the DS execution lock provides it instead.
 */
static OSIntrMode intrMode;
static OSIrqFunction irqHandlers[32];
static OSIrqMask irqMask;
static OSProcMode procMode = OS_PROCMODE_SYS;

OSIntrMode OS_DisableInterrupts(void)
{
	OSIntrMode old = intrMode;
	intrMode = 1;
	return old;
}

OSIntrMode OS_RestoreInterrupts(OSIntrMode mode)
{
	OSIntrMode old = intrMode;
	intrMode = mode;
	return old;
}

OSProcMode OS_GetProcMode(void)
{
	return VitaNativeAlarmInCallback() ? OS_PROCMODE_IRQ : procMode;
}

void OS_SetIrqFunction(OSIrqMask bits, OSIrqFunction fn)
{
	for (int i = 0; i < 32; i++)
		if (bits & (1u << i))
			irqHandlers[i] = fn;
}

OSIrqMask OS_EnableIrqMask(OSIrqMask bits)
{
	OSIrqMask old = irqMask;
	irqMask |= bits;
	return old;
}

OSIrqMask OS_DisableIrqMask(OSIrqMask bits)
{
	OSIrqMask old = irqMask;
	irqMask &= ~bits;
	return old;
}

OSIrqMask OS_SetIrqMask(OSIrqMask bits)
{
	OSIrqMask old = irqMask;
	irqMask = bits;
	return old;
}

OSIrqMask OS_ResetRequestIrqMask(OSIrqMask bits)
{
	OSIrqMask old = reg_OS_IF;
	reg_OS_IF &= ~bits;
	return old;
}

OSIrqFunction OS_GetIrqFunction(OSIrqMask bits)
{
	for (int i = 0; i < 32; i++)
		if (bits & (1u << i))
			return irqHandlers[i];
	return NULL;
}

/* The only interrupt the game waits for is vertical blank. The renderer supplies the wait; the DS
 * execution lock is dropped across it, because this is exactly where the DS would let another thread
 * run. */
void OS_WaitIrq(BOOL clear, OSIrqMask bits)
{
	extern void VitaNativeVBlankWait(void);
	extern void VitaNativeInputVBlank(void);
	unsigned depth;

	if (bits != OS_IE_V_BLANK) {
		char message[64];
		snprintf(message, sizeof message, "unsupported IRQ wait %08lx", (unsigned long)bits);
		PSPNativeFatal(message);
	}
	if (clear)
		s_HW_INTR_CHECK_BUF &= ~bits;

	depth = VitaOS_Release();
	VitaNativeVBlankWait();
	VitaOS_Reacquire(depth);

	VitaNativeInputVBlank();

	if (!intrMode && (irqMask & bits) && irqHandlers[0]) {
		OSProcMode old = procMode;
		procMode = OS_PROCMODE_IRQ;
		irqHandlers[0]();
		procMode = old;
	}
}

void OS_WaitVBlankIntr(void) { OS_WaitIrq(TRUE, OS_IE_V_BLANK); }

/* ---------------------------------------------------------------- failure paths */

void SIM_handleAssertionFailure(const char *file, unsigned line, const char *expr)
{
	char message[256];
	snprintf(message, sizeof message, "assertion %s:%u %s", file, line, expr);
	PSPNativeFatal(message);
}

void SIM_handleAssertionFailureMsg(const char *file, unsigned line, const char *fmt, ...)
{
	char detail[160], message[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(detail, sizeof detail, fmt, args);
	va_end(args);
	snprintf(message, sizeof message, "SDK assertion %s:%u %s", file, line, detail);
	PSPNativeFatal(message);
}

void ErrorHandling_AssertFail(void) { PSPNativeFatal("game assertion (ErrorHandling_AssertFail)"); }
void OS_Terminate(void) { PSPNativeFatal("OS_Terminate"); }
void OS_IrqHandler(void) { PSPNativeFatal("unsupported direct DS network IRQ handler"); }

void OS_ResetSystem(u32 parameter)
{
	char message[96];
	snprintf(message, sizeof message, "reset requested %lu; native restart unsupported",
	         (unsigned long)parameter);
	PSPNativeFatal(message);
}

void OS_SetDPermissionsForProtectionRegion(u32 mask, u32 flags)
{
	char message[96];
	snprintf(message, sizeof message, "unsupported DS cartridge protection %lx/%lx",
	         (unsigned long)mask, (unsigned long)flags);
	PSPNativeFatal(message);
}

/* The DS card's cache thresholds mean nothing here: the file system reads the ROM through ordinary
 * buffered I/O. Recorded so the startup log shows what the game asked for. */
static u32 cardIcacheThreshold, cardDcacheThreshold;

void CARD_SetCacheFlushThreshold(u32 icache, u32 dcache)
{
	cardIcacheThreshold = icache;
	cardDcacheThreshold = dcache;
	PSPNativeMemLog("[STARTUP] CARD cache thresholds %lu/%lu; synchronous stdio backend",
	                (unsigned long)icache, (unsigned long)dcache);
}
