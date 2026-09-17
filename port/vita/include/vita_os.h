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

/* The port's log. printf goes nowhere on a console, so everything that matters after boot goes to
 * a file next to the application. Implemented by the application, declared here so the platform
 * sources can report without depending on which game they are linked into. */
void VitaNativeMemLog(const char *fmt, ...);
void VitaNativeFatal(const char *message);

#ifdef __cplusplus
}
#endif
#endif /* VITAPOKE_VITA_OS_H */
