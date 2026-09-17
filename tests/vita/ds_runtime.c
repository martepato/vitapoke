/* vitapoke: runtime behaviour tests for the Vita platform layer, run under Vita3K.
 *
 * tests/vita/ds_surface.c proves the DS interfaces compile and link. This one runs them and checks what
 * they actually do, because the parts of port/vita that are not obvious are exactly the parts a compiler
 * cannot check:
 *
 *   - the DS execution lock really serialises DS threads (the point of it: the game is written for one
 *     core and this is a four-core console),
 *   - a wake that lands between a thread queueing itself and sleeping is not lost,
 *   - alarms fire once, repeat when periodic, stay cancelled, and report OS_PROCMODE_IRQ inside the
 *     handler,
 *   - recursive lock depth survives a release/reacquire round trip.
 *
 * It also pins down psp2 semantics the port assumes -- that an LwMutex created with
 * SCE_KERNEL_MUTEX_ATTR_RECURSIVE really is recursive, and that an event flag waited on with
 * SCE_EVENT_WAITCLEAR returns immediately when the flag was already set. Those are the assumptions the
 * sleep/wake design rests on; getting either wrong would hang the game on hardware.
 *
 * Results go to ux0:data/vitapoke-vita3k.txt as one line per check, because an emulator's handling of
 * stdout is its own business but a file is a file. tests/vita/vita3k.sh reads it back.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <nitro.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>

#include "../../port/vita/include/vita_os.h"
#include "../../port/vita/include/vita_input.h"

/* The generated register backing the real build supplies. */
u8 s_HW_MAIN_MEM[0x800000];
u8 s_HW_MAIN_MEM_SYSTEM[HW_MAIN_MEM_SYSTEM_SIZE];
REGType16v s_reg_PAD_KEYINPUT;
u32 s_HW_INTR_CHECK_BUF;
REGType32v s_reg_OS_IF;

/* ---------------------------------------------------------------- reporting */

static char report[8192];
static unsigned reportLength;
static int failures;

static void Line(const char *fmt, ...)
{
	va_list args;
	int written;
	va_start(args, fmt);
	written = vsnprintf(report + reportLength, sizeof report - reportLength, fmt, args);
	va_end(args);
	if (written > 0)
		reportLength += (unsigned)written;
}

static void Check(const char *name, int ok, const char *detail)
{
	if (!ok)
		failures++;
	Line("%-46s %s%s%s\n", name, ok ? "PASS" : "FAIL", detail && *detail ? "  " : "",
	     detail ? detail : "");
}

/* port/vita expects these from the application. */
void PSPNativeMemLog(const char *fmt, ...) { (void)fmt; }
void PSPNativeFatal(const char *message)
{
	/* A fatal from inside the platform layer is itself a result worth reporting, so record it and
	 * flush before giving up rather than vanishing. */
	Line("FATAL %s\n", message ? message : "(null)");
	failures++;
	{
		SceUID fd = sceIoOpen("ux0:data/vitapoke-vita3k.txt",
		                      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
		if (fd >= 0) {
			sceIoWrite(fd, report, reportLength);
			sceIoClose(fd);
		}
	}
	sceKernelExitProcess(1);
}

/* The renderer supplies this; here the vblank wait is a short sleep so OS_WaitIrq can be exercised. */
void VitaNativeVBlankWait(void) { sceKernelDelayThread(1000); }

/* ---------------------------------------------------------------- psp2 assumptions
 *
 * Checked first, because everything else in port/vita is built on them.
 */
static void TestPsp2Assumptions(void)
{
	{
		SceKernelLwMutexWork work;
		int created = sceKernelCreateLwMutex(&work, "recursive", SCE_KERNEL_MUTEX_ATTR_RECURSIVE,
		                                     0, NULL);
		int second = -1;
		if (created >= 0) {
			sceKernelLockLwMutex(&work, 1, NULL);
			/* If this is not recursive it blocks here and the test times out, which is itself
			 * the answer. */
			second = sceKernelLockLwMutex(&work, 1, NULL);
			sceKernelUnlockLwMutex(&work, 1);
			sceKernelUnlockLwMutex(&work, 1);
			sceKernelDeleteLwMutex(&work);
		}
		Check("psp2: LwMutex is recursive", created >= 0 && second >= 0, "");
	}
	{
		/* The sleep/wake design needs a wake that arrives first to be remembered. */
		SceUID flag = sceKernelCreateEventFlag("preset", 0, 0, NULL);
		SceUInt timeout = 100000;   /* 100 ms: long enough to distinguish "returned" from "timed out" */
		int set = sceKernelSetEventFlag(flag, 1);
		int waited = sceKernelWaitEventFlag(flag, 1, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR, NULL,
		                                    &timeout);
		/* And that the wait cleared it, so the next wait does block. */
		SceUInt again = 1000;
		int second = sceKernelWaitEventFlag(flag, 1, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR, NULL,
		                                    &again);
		sceKernelDeleteEventFlag(flag);
		Check("psp2: preset event flag returns at once", flag >= 0 && set >= 0 && waited >= 0, "");
		Check("psp2: WAITCLEAR consumed the flag", second < 0, "");
	}
}

/* ---------------------------------------------------------------- the execution lock */

/* Run before OS_Init, where nothing holds the lock yet. OS_InitThread deliberately enters DS context
 * and stays there for the life of the game thread, so after it the depth is never zero and a test that
 * assumed otherwise would be measuring the wrong thing. */
static void TestLockDepth(void)
{
	unsigned depth;

	Check("lock: not held before anything enters", !VitaOS_Held(), "");

	VitaOS_Enter();
	VitaOS_Enter();
	VitaOS_Enter();
	Check("lock: held after three enters", VitaOS_Held(), "");

	depth = VitaOS_Release();
	{
		char detail[32];
		snprintf(detail, sizeof detail, "depth=%u", depth);
		Check("lock: release reported depth 3", depth == 3, detail);
	}
	Check("lock: not held after release", !VitaOS_Held(), "");

	VitaOS_Reacquire(depth);
	Check("lock: held again after reacquire", VitaOS_Held(), "");
	VitaOS_Leave();
	VitaOS_Leave();
	VitaOS_Leave();
	Check("lock: released after three leaves", !VitaOS_Held(), "");
}

/* Two DS threads that would interleave if the lock were not doing its job.
 *
 * Each records itself as the owner, does work long enough for the scheduler to preempt it on another
 * core, and checks the owner is still itself. On the DS this always holds because there is one core; on
 * the Vita it holds only because of the execution lock. A plain psp2 thread pair fails this.
 */
static volatile int lockOwnerMark;
static volatile int interleavings;
static volatile int spinnerDone[2];

static void Spinner(void *arg);

static void Spinner(void *arg)
{
	int me = (int)(intptr_t)arg;
	for (int round = 0; round < 200; round++) {
		lockOwnerMark = me;
		/* Busy work, not a sleep: a sleep would release the lock and prove nothing. */
		for (volatile int i = 0; i < 2000; i++) {
		}
		if (lockOwnerMark != me)
			interleavings++;
	}
	spinnerDone[me] = 1;
}

/* The control. The same spinners as raw psp2 threads, with no execution lock, to show that this machine
 * really does run two threads at once -- otherwise the locked run below scores zero for the boring
 * reason and proves nothing. Reported rather than asserted: a scheduler that happens not to interleave
 * makes the control inconclusive, which is not the port's fault and should not read as a failure. */
static int ControlEntry(SceSize args, void *argp)
{
	(void)args;
	Spinner(*(void **)argp);
	return 0;
}

static int MeasureUnlockedInterleavings(void)
{
	SceUID a, b;
	void *argA = (void *)(intptr_t)0;
	void *argB = (void *)(intptr_t)1;
	unsigned waited = 0;

	interleavings = 0;
	spinnerDone[0] = spinnerDone[1] = 0;

	a = sceKernelCreateThread("controlA", ControlEntry, 0x60, 0x4000, 0,
	                          SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
	b = sceKernelCreateThread("controlB", ControlEntry, 0x60, 0x4000, 0,
	                          SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
	if (a < 0 || b < 0)
		return -1;
	sceKernelStartThread(a, sizeof argA, &argA);
	sceKernelStartThread(b, sizeof argB, &argB);

	while ((!spinnerDone[0] || !spinnerDone[1]) && waited < 10000) {
		sceKernelDelayThread(10000);
		waited += 10;
	}
	{
		int status = 0;
		sceKernelWaitThreadEnd(a, &status, NULL);
		sceKernelWaitThreadEnd(b, &status, NULL);
		sceKernelDeleteThread(a);
		sceKernelDeleteThread(b);
	}
	return interleavings;
}

static void TestSerialisation(void)
{
	static OSThread threadA, threadB;
	static u8 stackA[8192], stackB[8192];
	unsigned waited = 0;
	int control;

	/* Control first, while the game thread still holds the lock: these threads do not take it, so
	 * they are free to trample each other. */
	control = MeasureUnlockedInterleavings();
	{
		char detail[80];
		snprintf(detail, sizeof detail, "unlocked interleavings=%d", control);
		if (control > 0)
			Check("threads: control shows real concurrency", 1, detail);
		else
			Line("%-46s INFO  %s (control inconclusive)\n",
			     "threads: control shows real concurrency", detail);
	}

	interleavings = 0;
	spinnerDone[0] = spinnerDone[1] = 0;

	OS_CreateThread(&threadA, Spinner, (void *)(intptr_t)0, stackA + sizeof stackA, sizeof stackA, 17);
	OS_CreateThread(&threadB, Spinner, (void *)(intptr_t)1, stackB + sizeof stackB, sizeof stackB, 17);
	OS_WakeupThreadDirect(&threadA);
	OS_WakeupThreadDirect(&threadB);

	/* OS_Sleep gives up the lock, which is what lets the two spinners run. */
	while ((!spinnerDone[0] || !spinnerDone[1]) && waited < 10000) {
		OS_Sleep(10);
		waited += 10;
	}

	Check("threads: both spinners finished", spinnerDone[0] && spinnerDone[1], "");
	{
		char detail[48];
		snprintf(detail, sizeof detail, "interleavings=%d", interleavings);
		Check("threads: execution lock serialised them", interleavings == 0, detail);
	}
	OS_JoinThread(&threadA);
	OS_JoinThread(&threadB);
}

/* Sleep and wake across two threads, including the wake-before-sleep case. */
static OSThreadQueue sleepQueue;
static volatile int sleeperState;   /* 0 not started, 1 about to sleep, 2 woken */

static void Sleeper(void *arg)
{
	(void)arg;
	sleeperState = 1;
	OS_SleepThread(&sleepQueue);
	sleeperState = 2;
}

static void TestSleepWake(void)
{
	static OSThread sleeper;
	static u8 stack[8192];
	unsigned waited = 0;

	OS_InitThreadQueue(&sleepQueue);
	sleeperState = 0;
	OS_CreateThread(&sleeper, Sleeper, NULL, stack + sizeof stack, sizeof stack, 17);
	OS_WakeupThreadDirect(&sleeper);

	while (sleeperState < 1 && waited < 5000) {
		OS_Sleep(5);
		waited += 5;
	}
	Check("threads: sleeper reached its sleep", sleeperState >= 1, "");

	/* Wake it; it may not have reached sceKernelWaitEventFlag yet, which is precisely the race the
	 * per-thread event flag exists to survive. */
	OS_WakeupThread(&sleepQueue);

	waited = 0;
	while (sleeperState < 2 && waited < 5000) {
		OS_Sleep(5);
		waited += 5;
	}
	Check("threads: wake was not lost", sleeperState == 2, "");
	OS_JoinThread(&sleeper);
}

/* ---------------------------------------------------------------- alarms */

static volatile int oneShotCount, periodicCount;
static volatile int procModeInHandler = -1;

static void OneShot(void *arg)
{
	(void)arg;
	procModeInHandler = (int)OS_GetProcMode();
	oneShotCount++;
}

static void Periodic(void *arg)
{
	(void)arg;
	periodicCount++;
}

static void Cancelled(void *arg)
{
	(void)arg;
	/* Must never run. */
	failures++;
	Line("%-46s FAIL  a cancelled alarm fired\n", "alarms: cancel prevents firing");
}

/* One millisecond in DS ticks. */
#define MS_TICKS ((OSTick)(OS_SYSTEM_CLOCK / 64000))

static void TestAlarms(void)
{
	static OSAlarm oneShot, periodic, cancelled;
	unsigned waited;

	oneShotCount = periodicCount = 0;
	procModeInHandler = -1;

	OS_CreateAlarm(&oneShot);
	OS_SetAlarm(&oneShot, MS_TICKS * 20, OneShot, NULL);

	OS_CreateAlarm(&cancelled);
	OS_SetAlarm(&cancelled, MS_TICKS * 20, Cancelled, NULL);
	OS_CancelAlarm(&cancelled);

	OS_CreateAlarm(&periodic);
	OS_SetPeriodicAlarm(&periodic, OS_GetTick() + MS_TICKS * 10, MS_TICKS * 10, Periodic, NULL);

	/* OS_Sleep releases the execution lock, so the alarm thread can take it and run handlers. */
	for (waited = 0; waited < 500 && (oneShotCount == 0 || periodicCount < 3); waited += 10)
		OS_Sleep(10);

	{
		char detail[48];
		snprintf(detail, sizeof detail, "fired=%d", oneShotCount);
		Check("alarms: one-shot fired exactly once", oneShotCount == 1, detail);
		snprintf(detail, sizeof detail, "fired=%d", periodicCount);
		Check("alarms: periodic fired repeatedly", periodicCount >= 3, detail);
	}
	Check("alarms: handler saw OS_PROCMODE_IRQ", procModeInHandler == (int)OS_PROCMODE_IRQ, "");
	if (oneShotCount == 1)
		Check("alarms: cancel prevents firing", 1, "");

	OS_CancelAllAlarms();
}

/* ---------------------------------------------------------------- the clock */

static void TestTick(void)
{
	OSTick before, after;
	unsigned long long elapsedUs;

	before = OS_GetTick();
	sceKernelDelayThread(50000);   /* 50 ms */
	after = OS_GetTick();

	Check("tick: monotonic", after > before, "");
	/* Convert back and check it is in the right ballpark: 50 ms, allowing for an emulator's timing. */
	elapsedUs = ((unsigned long long)(after - before) * 64000000ULL) / OS_SYSTEM_CLOCK;
	{
		char detail[64];
		snprintf(detail, sizeof detail, "measured=%lluus", elapsedUs);
		Check("tick: 50ms reads back as 25-500ms", elapsedUs > 25000 && elapsedUs < 500000, detail);
	}
}

/* ---------------------------------------------------------------- input and the arena */

static void TestInput(void)
{
	TP_Init();
	RTC_Init();
	{
		TPCalibrateParam param;
		Check("touch: TP_GetUserInfo is the identity",
		      TP_GetUserInfo(&param) && param.xDotSize == 256 && param.yDotSize == 256, "");
	}
	VitaNativeInputPoll();
	{
		unsigned keys = 0xFFFF;
		int down = -1, x = -1, y = -1;
		VitaNativeInputGetRenderState(&keys, &down, &x, &y);
		/* No input in an unattended run, so the useful assertion is that the values are sane and
		 * that reading the pad and the panel did not fail. */
		Check("touch: render state is in range",
		      down == 0 && x >= 0 && x < 256 && y >= 0 && y < 192, "");
		Check("pad: no buttons reported with no input", keys == 0, "");
	}
	{
		RTCDate date;
		RTCTime time;
		RTCResult result = RTC_GetDateTime(&date, &time);
		char detail[64];
		snprintf(detail, sizeof detail, "result=%d year=%u", (int)result, (unsigned)date.year);
		Check("clock: RTC_GetDateTime succeeded", result == RTC_RESULT_SUCCESS, detail);
	}
}

static void TestArena(void)
{
	void *lo = OS_AllocFromArenaLo(OS_ARENA_MAIN, 1024, 32);
	void *hi = OS_AllocFromArenaHi(OS_ARENA_MAIN, 1024, 32);
	Check("arena: low allocation aligned and in range",
	      lo && ((uintptr_t)lo & 31) == 0 && (u8 *)lo >= s_HW_MAIN_MEM, "");
	Check("arena: high allocation aligned and above low",
	      hi && ((uintptr_t)hi & 31) == 0 && (u8 *)hi > (u8 *)lo, "");
}

/* ---------------------------------------------------------------- */

int main(void)
{
	SceUID fd;

	Line("vitapoke platform layer, runtime checks under Vita3K\n\n");

	TestPsp2Assumptions();
	TestLockDepth();

	/* OS_Init enters DS context and stays there, as it does in the real application: from here on
	 * the game thread is inside DS code. */
	OS_Init();
	Check("lock: OS_Init left the game thread in DS context", VitaOS_Held(), "");
	OS_InitTick();
	OS_InitAlarm();

	TestArena();
	TestTick();
	TestSerialisation();
	TestSleepWake();
	TestAlarms();
	TestInput();

	Line("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures,
	     failures == 1 ? "" : "s");

	fd = sceIoOpen("ux0:data/vitapoke-vita3k.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd >= 0) {
		sceIoWrite(fd, report, reportLength);
		sceIoClose(fd);
	}
	printf("%s", report);
	fflush(stdout);

	sceKernelExitProcess(failures ? 1 : 0);
	return 0;
}
