/* vitapoke: the DS SDK's alarms on the Vita.
 *
 * The game asks for a handler to run once, or periodically, at a tick deadline. On the DS that is a
 * hardware timer interrupt; the handler runs to completion with nothing else running, and code that
 * checks OS_GetProcMode sees OS_PROCMODE_IRQ while it does.
 *
 * Here one timer thread sleeps until the earliest deadline and runs whatever is due. It holds the DS
 * execution lock across each handler, which reproduces the part the game depends on: a handler cannot
 * interleave with the game thread, and shared state does not need locking inside it.
 *
 * One thread rather than one per alarm. The PSP build had a kernel alarm per OSAlarm because that is
 * what the PSP offered; psp2 has no alarm service at all, so the choice was free, and a single thread
 * waking on the earliest deadline is both cheaper and easier to reason about than thirty-two threads
 * each asleep on their own.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <nitro.h>
#include <psp2/kernel/threadmgr.h>

#include "include/vita_os.h"
#include "include/vita_input.h"

#define MAX_ALARMS 32

static OSAlarm *slots[MAX_ALARMS];
static BOOL ready;
static volatile BOOL inCallback;
static SceUID timerThread = -1;
static SceUID wake = -1;
static volatile int stopping;

/* os_core.c reports OS_PROCMODE_IRQ while this is set, which is what the DS would report inside a
 * timer interrupt. */
int VitaNativeAlarmInCallback(void) { return inCallback ? 1 : 0; }

/* DS ticks are the system clock over 64; the wait is in microseconds. Round up, so an alarm never
 * fires early and get rechecked against its absolute deadline when it does. */
static unsigned long long TicksToMicroseconds(OSTick ticks)
{
	return (ticks * 64000000ULL + OS_SYSTEM_CLOCK - 1) / OS_SYSTEM_CLOCK;
}

static OSAlarm **FindSlot(OSAlarm *alarm)
{
	for (int i = 0; i < MAX_ALARMS; i++)
		if (slots[i] == alarm)
			return &slots[i];
	return NULL;
}

/* Earliest deadline among the armed alarms, or 0 when none are armed. Caller holds the table lock. */
static BOOL EarliestDeadline(OSTick *out)
{
	BOOL any = FALSE;
	for (int i = 0; i < MAX_ALARMS; i++) {
		if (!slots[i] || !slots[i]->handler)
			continue;
		if (!any || slots[i]->fire < *out) {
			*out = slots[i]->fire;
			any = TRUE;
		}
	}
	return any;
}

static int TimerThread(SceSize args, void *argp)
{
	(void)args;
	(void)argp;

	while (!stopping) {
		OSTick deadline = 0;
		OSTick now;
		BOOL armed;

		VitaOS_TableLock();
		armed = EarliestDeadline(&deadline);
		VitaOS_TableUnlock();

		if (!armed) {
			/* Nothing to wait for: sleep until an alarm is set. */
			sceKernelWaitEventFlag(wake, 1, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR, NULL, NULL);
			continue;
		}

		now = OS_GetTick();
		if (now < deadline) {
			unsigned long long us = TicksToMicroseconds(deadline - now);
			SceUInt timeout = us > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (SceUInt)us;
			/* Woken early means the alarm set changed, so start again from the earliest. */
			sceKernelWaitEventFlag(wake, 1, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR, NULL, &timeout);
			continue;
		}

		/* Run everything now due. The execution lock is taken around the whole batch: on the DS
		 * these run in interrupt context, so nothing else may be running DS code. */
		VitaOS_Enter();
		for (int i = 0; i < MAX_ALARMS && !stopping; i++) {
			OSAlarm *alarm;
			OSAlarmHandler handler;
			void *arg;

			VitaOS_TableLock();
			alarm = slots[i];
			if (!alarm || !alarm->handler || alarm->fire > OS_GetTick()) {
				VitaOS_TableUnlock();
				continue;
			}
			handler = alarm->handler;
			arg = alarm->arg;
			if (alarm->period) {
				/* Advance past every period already missed, so a slow frame does not
				 * queue up a burst of catch-up calls. */
				OSTick when = OS_GetTick();
				do {
					alarm->fire += alarm->period;
				} while (alarm->fire <= when);
			} else {
				alarm->handler = NULL;
				slots[i] = NULL;
			}
			VitaOS_TableUnlock();

			inCallback = TRUE;
			handler(arg);
			inCallback = FALSE;
		}
		VitaOS_Leave();
	}
	return 0;
}

static void StartTimerThread(void)
{
	if (timerThread >= 0)
		return;
	wake = sceKernelCreateEventFlag("vitapoke_alarm", 0, 0, NULL);
	if (wake < 0)
		VitaNativeFatal("OS_InitAlarm: could not create the alarm event flag");
	/* Above the game thread: an alarm whose deadline has passed has to be able to run even while
	 * the game thread is busy, which is the whole point of it not being on that thread. */
	timerThread = sceKernelCreateThread("vitapoke_alarm", TimerThread, 0x40, 0x4000, 0,
	                                    SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
	if (timerThread < 0)
		VitaNativeFatal("OS_InitAlarm: could not create the alarm thread");
	sceKernelStartThread(timerThread, 0, NULL);
}

void OS_InitAlarm(void)
{
	if (ready)
		return;
	VitaOS_TableLock();
	memset(slots, 0, sizeof slots);
	VitaOS_TableUnlock();
	stopping = 0;
	StartTimerThread();
	ready = TRUE;
}

BOOL OS_IsAlarmAvailable(void) { return ready; }

void OS_CreateAlarm(OSAlarm *alarm) { memset(alarm, 0, sizeof *alarm); }

void OS_SetAlarm(OSAlarm *alarm, OSTick tick, OSAlarmHandler handler, void *arg)
{
	OSAlarm **slot = NULL;

	if (!ready || !alarm || !handler)
		VitaNativeFatal("OS_SetAlarm: alarms unavailable or bad argument");

	VitaOS_TableLock();
	if (FindSlot(alarm)) {
		VitaOS_TableUnlock();
		VitaNativeFatal("OS_SetAlarm: alarm is already set");
	}
	for (int i = 0; i < MAX_ALARMS; i++)
		if (!slots[i]) {
			slot = &slots[i];
			break;
		}
	if (!slot) {
		VitaOS_TableUnlock();
		VitaNativeFatal("OS_SetAlarm: no free alarm slot");
	}
	alarm->handler = handler;
	alarm->arg = arg;
	alarm->period = 0;
	alarm->fire = OS_GetTick() + tick;
	*slot = alarm;
	VitaOS_TableUnlock();

	sceKernelSetEventFlag(wake, 1);
}

void OS_SetPeriodicAlarm(OSAlarm *alarm, OSTick start, OSTick period, OSAlarmHandler handler, void *arg)
{
	OSTick now, next;

	if (!period)
		VitaNativeFatal("OS_SetPeriodicAlarm: zero period");

	now = OS_GetTick();
	next = start;
	if (next <= now)
		next += ((now - next) / period + 1) * period;

	OS_SetAlarm(alarm, next - now, handler, arg);

	VitaOS_TableLock();
	alarm->start = start;
	alarm->period = period;
	alarm->fire = next;
	VitaOS_TableUnlock();

	sceKernelSetEventFlag(wake, 1);
}

void OS_SetAlarmTag(OSAlarm *alarm, u32 tag)
{
	if (!tag)
		VitaNativeFatal("OS_SetAlarmTag: zero tag");
	alarm->tag = tag;
}

void OS_CancelAlarm(OSAlarm *alarm)
{
	OSAlarm **slot;

	VitaOS_TableLock();
	slot = FindSlot(alarm);
	if (slot)
		*slot = NULL;
	alarm->handler = NULL;
	alarm->period = 0;
	VitaOS_TableUnlock();

	/* Wake the timer thread so it recomputes its deadline without this alarm. */
	if (wake >= 0)
		sceKernelSetEventFlag(wake, 1);
}

void OS_CancelAlarms(u32 tag)
{
	if (!tag)
		VitaNativeFatal("OS_CancelAlarms: zero tag");
	for (int i = 0; i < MAX_ALARMS; i++) {
		OSAlarm *alarm;
		VitaOS_TableLock();
		alarm = slots[i];
		VitaOS_TableUnlock();
		if (alarm && alarm->tag == tag)
			OS_CancelAlarm(alarm);
	}
}

void OS_CancelAllAlarms(void)
{
	for (int i = 0; i < MAX_ALARMS; i++) {
		OSAlarm *alarm;
		VitaOS_TableLock();
		alarm = slots[i];
		VitaOS_TableUnlock();
		if (alarm)
			OS_CancelAlarm(alarm);
	}
}

void OS_EndAlarm(void)
{
	OS_CancelAllAlarms();
	ready = FALSE;
	stopping = 1;
	if (wake >= 0)
		sceKernelSetEventFlag(wake, 1);
}
