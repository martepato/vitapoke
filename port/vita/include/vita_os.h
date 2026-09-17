/* vitapoke: interfaces shared between the Vita platform sources in port/vita.
 *
 * Nothing outside port/vita includes this. The game and the DS SDK see only the DS interfaces
 * (OS_*, TP_*, RTC_*) that these files implement.
 *
 * ------------------------------------------------------------------ the DS execution lock
 *
 * The DS is a single core, and the game and the SDK are written for one: their threads take turns,
 * never run at once, and protect shared state by stopping the scheduler rather than by locking. The PSP
 * inherited that for free -- one core, and sceKernelSuspendDispatchThread to stop a switch. The Vita has
 * four cores and will genuinely run two DS threads at the same time on two of them, which is a class of
 * race the PSP build cannot have and cannot have been tested against.
 *
 * So this port makes the DS's guarantee explicit: one lock, held by whichever DS thread is running DS
 * code. A thread releases it only where the DS would have yielded -- sleeping, yielding, waiting for
 * an interrupt -- and takes it again on the way back. OS_DisableScheduler then costs nothing, because
 * holding the lock is already the guarantee it was asking for.
 *
 * This is deliberately not the PSP's arrangement translated. Mapping "stop the scheduler" onto a
 * priority change, or onto nothing at all, would compile and would look like it worked.
 */
#ifndef VITAPOKE_VITA_OS_H
#define VITAPOKE_VITA_OS_H

#include <psp2/kernel/threadmgr.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Take and release the DS execution lock. Recursive: a DS call that takes it and then calls another
 * that does the same must not deadlock. Every DS thread this port creates holds it while it runs. */
void VitaOS_Enter(void);
void VitaOS_Leave(void);

/* Release the lock across a blocking wait and take it again afterwards, which is what the DS does
 * whenever it yields. Returns the recursion depth that was released, for VitaOS_Reacquire. */
unsigned VitaOS_Release(void);
void VitaOS_Reacquire(unsigned depth);

/* True when the caller holds the lock. Used by assertions, not by control flow. */
int VitaOS_Held(void);

/* Guards the tables in os_alarm.c and os_thread.c. Separate from the execution lock because it is
 * taken by code that has not entered DS context yet: the first OS_CreateThread comes from the Vita's
 * own main thread. */
void VitaOS_TableLock(void);
void VitaOS_TableUnlock(void);

/* Monotonic microseconds, on one clock for the whole port. */
unsigned long long VitaOS_Now(void);

/* ------------------------------------------------------------------ the stall watchdog
 *
 * Every place this port can wait bumps one of these, and the watchdog in watchdog.c prints the
 * counts when frames stop arriving. A hang is otherwise invisible: the log's last line is whatever
 * the game was doing a moment before, the console shows the last frame composed, and there is no
 * debugger. Which counter is still climbing says whether the port is spinning, whether it is
 * blocked on a wait that nothing will satisfy, and which one.
 */
enum VitaWaitSite {
	VITA_WAIT_VBLANK,        /* OS_WaitIrq: the game's own clock */
	VITA_WAIT_SLEEP,         /* OS_Sleep* and the SDK's delays */
	VITA_WAIT_YIELD,         /* OS_YieldThread */
	VITA_WAIT_JOIN,          /* OS_JoinThread */
	VITA_WAIT_QUEUE_SEND,    /* OS_SendMessage on a full queue */
	VITA_WAIT_QUEUE_RECV,    /* OS_ReceiveMessage on an empty one */
	VITA_WAIT_MUTEX,         /* OS_LockMutex, contended */
	VITA_WAIT_SPIN,          /* OS_SpinWait: a busy wait in the original */
	VITA_WAIT_CARD,          /* CARD_*: a read or a backup write */
	VITA_WAIT_SITES
};

/* Called on every wait. One increment, unsynchronised on purpose: a lost count costs nothing and a
 * lock here would be a lock on the hottest path in the port. */
void VitaOS_WaitTick(enum VitaWaitSite site);

/* Check that libc's free-list heads are still intact, and report the frame they stopped being so.
 * Called once a frame; see the note in watchdog.c for why this is worth its 258 loads. */
void VitaNativeHeapCheck(unsigned frame);

/* Start the watchdog. Does nothing if it is already running. */
void VitaNativeWatchdogStart(void);

/* The lock's state, for the watchdog's report. */
void VitaOS_LockState(SceUID *owner, unsigned *depth);

/* The DS threads this port has created, newest last, for the watchdog's report. Returns how many
 * were written. */
unsigned VitaOS_ThreadIds(SceUID *out, unsigned max);

/* The port's log. printf goes nowhere on a console, so everything that matters after boot goes to
 * a file next to the application. Implemented by the application, declared here so the platform
 * sources can report without depending on which game they are linked into. */
void VitaNativeMemLog(const char *fmt, ...);
void VitaNativeFatal(const char *message);

#ifdef __cplusplus
}
#endif
#endif /* VITAPOKE_VITA_OS_H */
