/* vitapoke: the DS SDK's threads on the Vita.
 *
 * The DS gives the game cooperative threads on one core: a switch happens only where the game asks for
 * one, and OS_DisableScheduler makes even that stop. Code written against it shares state between
 * threads with no locking, because it cannot be preempted.
 *
 * The Vita has four cores and a preemptive scheduler, so running these threads as ordinary psp2 threads
 * would let two of them execute DS code at once. That is not a translation problem, it is a behaviour
 * change, and it would show up as rare corruption rather than as a failure to build.
 *
 * So every DS thread here holds the DS execution lock (include/vita_os.h) while it runs, and gives it
 * up exactly where the DS would have switched: OS_SleepThread, OS_YieldThread, OS_Sleep, and the vblank
 * wait in os_core.c. The threads are real psp2 threads and the kernel still schedules them, but only
 * one is ever inside DS code, which is the guarantee the game was written against.
 *
 * OS_DisableScheduler therefore has nothing to do but count: holding the lock is already the promise it
 * wanted. It is still tracked, because the DS SDK treats sleeping with the scheduler disabled as a
 * programming error and so should this.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <nitro.h>
#include <psp2/kernel/threadmgr.h>

#include "include/vita_os.h"

#define MAX_THREADS 32

/* DS priorities are 0..31 (0 highest). Vita user priorities are 64..191. Map the DS range into the
 * middle of it, leaving room above for the alarm thread and below for anything that should yield. */
#define VITA_PRIORITY(dsPriority) (0x60 + (int)(dsPriority))

typedef struct Record {
	OSThread *thread;
	SceUID id;
	SceUID wake;             /* see the note on OS_SleepThread */
	void (*entry)(void *);
	void *arg;
	BOOL started;
} Record;

static Record records[MAX_THREADS];
static OSThread launcher;
static BOOL ready;
static u32 schedulerDepth;

OSThreadInfo OSi_ThreadInfo;

static void Check(int result, const char *what)
{
	if (result < 0) {
		char message[96];
		snprintf(message, sizeof message, "thread %s failed %08x", what, (unsigned)result);
		VitaNativeFatal(message);
	}
}

static Record *Find(const OSThread *thread)
{
	for (int i = 0; i < MAX_THREADS; i++)
		if (records[i].thread == thread)
			return &records[i];
	return NULL;
}

static Record *FindById(SceUID id)
{
	for (int i = 0; i < MAX_THREADS; i++)
		if (records[i].thread && records[i].id == id)
			return &records[i];
	return NULL;
}

/* psp2 has no sceKernelSleepThread/sceKernelWakeupThread, and its signal pair holds a single pending
 * signal whose second send is an error. An event flag per thread says what is wanted without that
 * corner: a wake sets the flag, a sleep waits and clears it, and a wake that lands just before the
 * sleep leaves the flag set so the sleep returns at once. That last property is the one OS_SleepThread
 * depends on -- the DS enqueues itself and only then sleeps -- and losing it would hang the game. */
static SceUID WakeFlag(const char *what)
{
	SceUID flag = sceKernelCreateEventFlag("vitapoke_wake", 0, 0, NULL);
	if (flag < 0) {
		char message[96];
		snprintf(message, sizeof message, "thread %s: could not create the wake flag", what);
		VitaNativeFatal(message);
	}
	return flag;
}

void OS_InitThread(void)
{
	if (ready)
		return;
	memset(&launcher, 0, sizeof launcher);
	launcher.id = sceKernelGetThreadId();
	launcher.priority = 16;
	launcher.state = OS_THREAD_STATE_READY;
	records[0].thread = &launcher;
	records[0].id = (SceUID)launcher.id;
	records[0].wake = WakeFlag("launcher");
	records[0].started = TRUE;
	OSi_ThreadInfo.list = OSi_ThreadInfo.current = &launcher;
	ready = TRUE;
	/* The thread that runs OS_Init is the game thread, and from here on it is inside DS code. */
	VitaOS_Enter();
}

BOOL OS_IsThreadAvailable(void) { return ready; }

OSThread *WIN_OS_GetCurrentThread(void)
{
	SceUID id;
	OS_InitThread();
	id = sceKernelGetThreadId();
	{
		Record *record = FindById(id);
		if (record)
			return record->thread;
	}
	VitaNativeFatal("WIN_OS_GetCurrentThread: caller is not a DS thread");
	return NULL;
}

static void Dequeue(OSThread *thread)
{
	OSThreadQueue *queue = thread->queue;
	if (!queue)
		return;
	if (thread->link.prev)
		thread->link.prev->link.next = thread->link.next;
	else
		queue->head = thread->link.next;
	if (thread->link.next)
		thread->link.next->link.prev = thread->link.prev;
	else
		queue->tail = thread->link.prev;
	thread->queue = NULL;
	thread->link.prev = thread->link.next = NULL;
}

void OS_ExitThread(void)
{
	OSThread *thread = WIN_OS_GetCurrentThread();
	if (thread->destructor)
		thread->destructor(thread->userParameter);
	thread->state = OS_THREAD_STATE_TERMINATED;
	/* Give up DS context before leaving: the thread is gone and must not keep the lock. */
	VitaOS_Release();
	sceKernelExitThread(0);
	VitaNativeFatal("OS_ExitThread: returned from sceKernelExitThread");
}

static int ThreadEntry(SceSize args, void *argp)
{
	Record *record = *(Record **)argp;
	(void)args;

	/* Enter DS context and stay in it for the life of the thread, apart from the places that
	 * explicitly yield. */
	VitaOS_Enter();
	record->thread->state = OS_THREAD_STATE_READY;
	record->entry(record->arg);
	OS_ExitThread();
	return 0;
}

void OS_CreateThreadReal(OSThread *thread, void (*entry)(void *), void *arg, void *stack, u32 size,
                         u32 priority)
{
	Record *record = NULL;

	(void)stack;   /* the DS stack stays data; the Vita kernel owns the real one */
	OS_InitThread();
	if (!thread || !entry || priority > 31 || Find(thread))
		VitaNativeFatal("OS_CreateThread: bad argument or duplicate thread");

	VitaOS_TableLock();
	for (int i = 1; i < MAX_THREADS; i++)
		if (!records[i].thread) {
			record = &records[i];
			break;
		}
	if (!record) {
		VitaOS_TableUnlock();
		VitaNativeFatal("OS_CreateThread: no free thread slot");
	}

	memset(thread, 0, sizeof *thread);
	thread->priority = priority;
	thread->state = OS_THREAD_STATE_WAITING;
	thread->context.func = (u64)(uintptr_t)entry;
	thread->context.arg = (u64)(uintptr_t)arg;

	record->id = sceKernelCreateThread("vitapoke_ds", ThreadEntry, VITA_PRIORITY(priority),
	                                   size < 16384 ? 16384 : size, 0,
	                                   SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
	if (record->id < 0) {
		VitaOS_TableUnlock();
		Check(record->id, "create");
	}
	record->wake = WakeFlag("create");
	record->thread = thread;
	record->entry = entry;
	record->arg = arg;
	record->started = FALSE;
	thread->id = record->id;
	thread->next = OSi_ThreadInfo.list;
	OSi_ThreadInfo.list = thread;
	VitaOS_TableUnlock();
}

void OS_CreateThreadDebug(OSThread *thread, void (*entry)(void *), void *arg, void *stack, u32 size,
                          u32 priority, const char *name, const char *file, u32 line)
{
	(void)name;
	(void)file;
	(void)line;
	OS_CreateThreadReal(thread, entry, arg, stack, size, priority);
}

void OS_WakeupThreadDirect(OSThread *thread)
{
	Record *record;
	BOOL start;

	record = Find(thread);
	if (!record || thread->state == OS_THREAD_STATE_TERMINATED)
		return;

	VitaOS_TableLock();
	if (record->started && thread->state != OS_THREAD_STATE_WAITING) {
		VitaOS_TableUnlock();
		return;
	}
	Dequeue(thread);
	thread->state = OS_THREAD_STATE_READY;
	start = !record->started;
	record->started = TRUE;
	VitaOS_TableUnlock();

	if (start) {
		Record *arg = record;
		Check(sceKernelStartThread(record->id, sizeof arg, &arg), "start");
	} else {
		Check(sceKernelSetEventFlag(record->wake, 1), "wake");
	}
}

void OS_SleepThread(OSThreadQueue *queue)
{
	OSThread *thread = WIN_OS_GetCurrentThread();
	Record *record = Find(thread);
	unsigned depth;

	if (!record)
		VitaNativeFatal("OS_SleepThread: caller is not a DS thread");
	if (schedulerDepth)
		VitaNativeFatal("OS_SleepThread: called with the scheduler disabled");

	VitaOS_TableLock();
	if (thread->queue)
		VitaNativeFatal("OS_SleepThread: thread is already queued");
	if (queue) {
		thread->queue = queue;
		thread->link.prev = queue->tail;
		thread->link.next = NULL;
		if (queue->tail)
			queue->tail->link.next = thread;
		else
			queue->head = thread;
		queue->tail = thread;
	}
	thread->state = OS_THREAD_STATE_WAITING;
	VitaOS_TableUnlock();

	/* Leave DS context for the duration: this is one of the points where the DS would switch, and
	 * holding the lock across the sleep would stop anything else running -- including whoever is
	 * supposed to wake us. psp2 keeps a pending wakeup, so a wake between queueing and sleeping is
	 * not lost. */
	depth = VitaOS_Release();
	Check(sceKernelWaitEventFlag(record->wake, 1, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR, NULL, NULL),
	      "sleep");
	VitaOS_Reacquire(depth);

	thread->state = OS_THREAD_STATE_READY;
}

void OS_SleepThreadDirect(OSThread *thread, OSThreadQueue *queue)
{
	if (thread != WIN_OS_GetCurrentThread())
		VitaNativeFatal("OS_SleepThreadDirect: not the calling thread");
	OS_SleepThread(queue);
}

void OS_WakeupThread(OSThreadQueue *queue)
{
	if (!queue)
		return;
	while (queue->head)
		OS_WakeupThreadDirect(queue->head);
}

void OS_JoinThread(OSThread *thread)
{
	Record *record = Find(thread);
	OSThread **link;
	unsigned depth;
	int status = 0;

	if (!record)
		return;
	if (!record->started || thread == WIN_OS_GetCurrentThread())
		VitaNativeFatal("OS_JoinThread: thread not started, or joining itself");

	/* The thread being joined needs DS context to finish, so the joiner must not hold it. */
	depth = VitaOS_Release();
	Check(sceKernelWaitThreadEnd(record->id, &status, NULL), "join");
	VitaOS_Reacquire(depth);

	Check(sceKernelDeleteThread(record->id), "delete");
	sceKernelDeleteEventFlag(record->wake);

	VitaOS_TableLock();
	link = &OSi_ThreadInfo.list;
	while (*link && *link != thread)
		link = &(*link)->next;
	if (*link)
		*link = thread->next;
	memset(record, 0, sizeof *record);
	VitaOS_TableUnlock();
}

BOOL OS_IsThreadTerminated(const OSThread *thread)
{
	return thread && thread->state == OS_THREAD_STATE_TERMINATED;
}

BOOL OS_SetThreadPriority(OSThread *thread, u32 priority)
{
	Record *record = Find(thread);
	if (!record || priority > 31)
		return FALSE;
	if (sceKernelChangeThreadPriority(record->id, VITA_PRIORITY(priority)) < 0)
		return FALSE;
	thread->priority = priority;
	return TRUE;
}

u32 OS_GetThreadPriority(const OSThread *thread) { return thread->priority; }

void OS_Sleep(u32 ms)
{
	unsigned depth;

	if (schedulerDepth)
		VitaNativeFatal("OS_Sleep: called with the scheduler disabled");

	depth = VitaOS_Release();
	while (ms) {
		u32 part = ms > 1000000 ? 1000000 : ms;
		Check(sceKernelDelayThread(part * 1000), "delay");
		ms -= part;
	}
	VitaOS_Reacquire(depth);
}

void OS_YieldThread(void)
{
	unsigned depth;

	if (schedulerDepth)
		return;
	/* Dropping the lock and taking it again is the yield: any DS thread waiting for it gets its
	 * turn, which is what the DS's scheduler would have done here. */
	depth = VitaOS_Release();
	sceKernelDelayThread(0);
	VitaOS_Reacquire(depth);
}

void OS_RescheduleThread(void) { OS_YieldThread(); }

/* Holding the execution lock is already the guarantee these asked for, so there is nothing to do but
 * count -- and refuse to sleep while the count is up, as the DS SDK does. */
u32 OS_DisableScheduler(void)
{
	u32 previous = schedulerDepth;
	schedulerDepth++;
	return previous;
}

u32 OS_EnableScheduler(void)
{
	u32 previous = schedulerDepth;
	if (schedulerDepth)
		schedulerDepth--;
	return previous;
}

OSThread *OS_GetThread(u32 id)
{
	for (OSThread *thread = OSi_ThreadInfo.list; thread; thread = thread->next)
		if (thread->id == id)
			return thread;
	return NULL;
}

int OS_GetNumberOfThread(void)
{
	int count = 0;
	for (OSThread *thread = OSi_ThreadInfo.list; thread; thread = thread->next)
		count++;
	return count;
}

BOOL OS_IsThreadInList(const OSThread *thread) { return Find(thread) != NULL; }

void OS_SetThreadDestructor(OSThread *thread, OSThreadDestructor destructor)
{
	thread->destructor = destructor;
}

OSThreadDestructor OS_GetThreadDestructor(const OSThread *thread) { return thread->destructor; }
void OS_SetThreadParameter(OSThread *thread, void *parameter) { thread->userParameter = parameter; }
void *OS_GetThreadParameter(const OSThread *thread) { return thread->userParameter; }
