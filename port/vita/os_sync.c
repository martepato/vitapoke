/* vitapoke: the DS SDK's message queues, mutexes and cache maintenance on the Vita.
 *
 * These are the DS synchronisation primitives the game and the sound engine use between DS threads.
 * They are not a translation of the PSP build's port/native-audio-sound/os_sync.c: on the PSP a DS
 * thread that blocked here simply stopped, because nothing else could be running DS code anyway. On
 * the Vita the DS execution lock (see include/vita_os.h) is what makes DS threads take turns, so
 * every blocking wait in this file has to release it and take it back afterwards. A DS thread that
 * blocked while holding it would stop every other DS thread with it, which on the DS is exactly what
 * these calls do not do: OS_ReceiveMessage on an empty queue sleeps the caller and lets the sender
 * run.
 *
 * That is the whole substance of this file. Everything else is bookkeeping around psp2 semaphores.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <nitro.h>
#include <psp2/kernel/threadmgr.h>

#include "include/vita_os.h"

/* ---------------------------------------------------------------- blocking under the lock */

/* Wait on a semaphore the way the DS waits: outside DS context, so another DS thread can run and
 * eventually make the wait succeed. `block` false is a poll and never yields.
 *
 * Returns non-zero if the count was taken.
 */
static int Take(SceUID sema, int block)
{
	int result;
	unsigned depth;

	if (!block)
		return sceKernelPollSema(sema, 1) >= 0;
	depth = VitaOS_Release();
	result = sceKernelWaitSema(sema, 1, NULL);
	VitaOS_Reacquire(depth);
	return result >= 0;
}

static void Give(SceUID sema)
{
	if (sceKernelSignalSema(sema, 1) < 0)
		VitaNativeFatal("OS semaphore signal failed");
}

static SceUID NewSema(const char *name, int initial, int maximum)
{
	SceUID id = sceKernelCreateSema(name, 0, initial, maximum, NULL);

	if (id < 0)
		VitaNativeFatal("OS semaphore creation failed");
	return id;
}

/* ---------------------------------------------------------------- message queues
 *
 * The DS queue is the array the caller supplies plus a count and a first index, and the SDK's own
 * code reads those fields directly. So the array stays where the DS put it and only the waiting is
 * ours: one gate for the indices, and a pair of counting semaphores for "has a message" and "has
 * room". libntr's OSMessageQueue carries an SDL semaphore pointer for the desktop simulator; this
 * port stores its own record there.
 */
typedef struct {
	SceUID gate, readable, writable;
} Queue;

void OS_InitMessageQueue(OSMessageQueue *q, OSMessage *array, s32 capacity)
{
	Queue *r;

	if (!q || !array || capacity <= 0)
		VitaNativeFatal("OS_InitMessageQueue with no array");
	memset(q, 0, sizeof(*q));
	r = malloc(sizeof(*r));
	if (!r)
		VitaNativeFatal("out of memory for a DS message queue");
	r->gate = NewSema("vitapoke_q_gate", 1, 1);
	r->readable = NewSema("vitapoke_q_read", 0, capacity);
	r->writable = NewSema("vitapoke_q_write", capacity, capacity);
	q->msgArray = array;
	q->msgCount = capacity;
	q->semaphore = (SDL_sem *)r;
}

static BOOL Send(OSMessageQueue *q, OSMessage msg, s32 flags, BOOL front)
{
	Queue *r = (Queue *)q->semaphore;
	unsigned pos;

	if (!r)
		VitaNativeFatal("send on an uninitialised DS message queue");
	if (!Take(r->writable, flags & OS_MESSAGE_BLOCK))
		return FALSE;
	if (!Take(r->gate, 1))
		VitaNativeFatal("DS message queue gate lost");
	if (front) {
		q->firstIndex = (q->firstIndex + q->msgCount - 1) % q->msgCount;
		pos = q->firstIndex;
	} else {
		pos = (q->firstIndex + q->usedCount) % q->msgCount;
	}
	q->msgArray[pos] = msg;
	q->usedCount++;
	Give(r->gate);
	Give(r->readable);
	return TRUE;
}

BOOL OS_SendMessage(OSMessageQueue *q, OSMessage msg, s32 flags) { return Send(q, msg, flags, FALSE); }
BOOL OS_JamMessage(OSMessageQueue *q, OSMessage msg, s32 flags) { return Send(q, msg, flags, TRUE); }

static BOOL Receive(OSMessageQueue *q, OSMessage *out, s32 flags, BOOL peek)
{
	Queue *r = (Queue *)q->semaphore;

	if (!r)
		VitaNativeFatal("receive on an uninitialised DS message queue");
	if (!Take(r->readable, flags & OS_MESSAGE_BLOCK))
		return FALSE;
	if (!Take(r->gate, 1))
		VitaNativeFatal("DS message queue gate lost");
	if (out)
		*out = q->msgArray[q->firstIndex];
	if (!peek) {
		q->firstIndex = (q->firstIndex + 1) % q->msgCount;
		q->usedCount--;
	}
	Give(r->gate);
	/* A peek leaves the message in place, so the count it took has to go back. */
	Give(peek ? r->readable : r->writable);
	return TRUE;
}

BOOL OS_ReceiveMessage(OSMessageQueue *q, OSMessage *out, s32 flags) { return Receive(q, out, flags, FALSE); }
BOOL OS_ReadMessage(OSMessageQueue *q, OSMessage *out, s32 flags) { return Receive(q, out, flags, TRUE); }

/* ---------------------------------------------------------------- mutexes
 *
 * Recursive, as the DS's are, and the DS's `count` field is kept up to date because the SDK reads it.
 */
typedef struct {
	SceUID gate;
	SceUID owner;
	unsigned depth;
} Mutex;

void OS_InitMutex(OSMutex *m)
{
	Mutex *r;

	if (!m)
		VitaNativeFatal("OS_InitMutex with no mutex");
	memset(m, 0, sizeof(*m));
	r = calloc(1, sizeof(*r));
	if (!r)
		VitaNativeFatal("out of memory for a DS mutex");
	r->gate = NewSema("vitapoke_mutex", 1, 1);
	r->owner = -1;
	m->sdlMutex = (SDL_mutex *)r;
}

BOOL OS_TryLockMutex(OSMutex *m)
{
	Mutex *r = (Mutex *)m->sdlMutex;
	SceUID me = sceKernelGetThreadId();

	if (r->owner != me) {
		if (!Take(r->gate, 0))
			return FALSE;
		r->owner = me;
	}
	r->depth++;
	m->count = r->depth;
	return TRUE;
}

void OS_LockMutex(OSMutex *m)
{
	Mutex *r = (Mutex *)m->sdlMutex;
	SceUID me = sceKernelGetThreadId();

	if (r->owner != me) {
		if (!Take(r->gate, 1))
			VitaNativeFatal("DS mutex lock failed");
		r->owner = me;
	}
	r->depth++;
	m->count = r->depth;
}

void OS_UnlockMutex(OSMutex *m)
{
	Mutex *r = (Mutex *)m->sdlMutex;

	if (r->owner != sceKernelGetThreadId() || !r->depth)
		VitaNativeFatal("DS mutex unlocked by a thread that does not hold it");
	m->count = --r->depth;
	if (!r->depth) {
		r->owner = -1;
		Give(r->gate);
	}
}

/* ---------------------------------------------------------------- cache maintenance
 *
 * The DS flushes the ARM9 data cache so the graphics and sound hardware, which read main memory
 * directly, see what the CPU just wrote. The PSP had the same arrangement and the same calls.
 *
 * The Vita does not expose cache maintenance to an application at all: psp2's Dcache functions are
 * kernel only. It does not need to. Nothing on this console reads the DS's memory behind the port's
 * back: the "hardware" that consumes it is the port's own code, running on the same coherent cores,
 * and the memory the GPU really does read is memory the renderer allocates with the attributes GXM
 * requires and maps itself. So these are not silently dropped calls: there is nothing to flush.
 *
 * The ordering they also imply does still matter, because the Vita reorders where the DS could not,
 * so each one is a data memory barrier. Inner-shareable covers the other three cores and the GPU's
 * view of the memory this port writes.
 */
static inline void Barrier(void) { __asm__ volatile("dmb ish" ::: "memory"); }

void DC_FlushRange(const void *p, u32 n) { (void)p; (void)n; Barrier(); }
void DC_StoreRange(const void *p, u32 n) { (void)p; (void)n; Barrier(); }
void DC_InvalidateRange(void *p, u32 n) { (void)p; (void)n; Barrier(); }
void DC_FlushAll(void) { Barrier(); }
void DC_WaitWriteBufferEmpty(void) { Barrier(); }

/* ---------------------------------------------------------------- odds and ends */

/* The DS SDK asks this to decide whether to use the Nitro emulator's private debug interface at
 * 0x04FFF200. There is no such thing here. */
BOOL OS_IsRunOnEmulator(void) { return FALSE; }

/* A busy wait measured in DS CPU cycles. Sleeping is right rather than spinning -- the work the
 * caller is waiting for is done by another thread of this port, which needs a core to run on -- and
 * the DS execution lock has to come off for that thread to be allowed to run at all. */
void OS_SpinWait(u32 cycles)
{
	u64 us = ((u64)cycles * 1000000u + OS_SYSTEM_CLOCK - 1) / OS_SYSTEM_CLOCK;

	if (us) {
		unsigned depth = VitaOS_Release();
		sceKernelDelayThread((SceUInt)us);
		VitaOS_Reacquire(depth);
	}
}
