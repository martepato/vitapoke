/* vitapoke: the stall watchdog.
 *
 * A port of a game to a console with no debugger fails in one of two ways. It crashes, and
 * memlog.c's replacement abort and the SDK's assertion paths write down where. Or it stops, and
 * nothing is written down at all: the last log line is whatever the game happened to be doing, the
 * screen keeps showing the last frame that was composed, and the only outward sign is that the
 * console is warm. This file exists for the second kind.
 *
 * It is one low-priority thread. Every two seconds it looks at the frame counter; if that has not
 * moved for STALL_SECONDS it writes a report, and it keeps writing one every REPEAT_SECONDS for as
 * long as the stall lasts, so a log tells the difference between stopped and merely slow. The
 * report is the three things worth knowing and cannot be got any other way on this console:
 *
 *   - which of the port's wait sites are still being entered, and how fast. A count that climbs is
 *     a spin; counts that are all still are a thread blocked on something that will never come.
 *   - who holds the DS execution lock, and how deep. A stall with the lock held by a thread that is
 *     itself waiting is the shape of a deadlock this port could introduce; a stall with the lock
 *     free is the game's own doing.
 *   - every DS thread's kernel status, which says whether they are running or waiting.
 *
 * It costs one increment per wait in the port's hot paths and a thread that sleeps. It is in every
 * build, because the failure it explains is the one most likely to be reported as "it just stops".
 */
#include <stdio.h>
#include <string.h>

#include <psp2/kernel/threadmgr.h>

#include "include/vita_os.h"

#define STALL_SECONDS  8
#define REPEAT_SECONDS 30
#define POLL_US        (2 * 1000 * 1000)
#define MAX_REPORTED   16

/* Frames completed, from frame.c: the one number that says the port is still going round. */
extern unsigned VitaNativeFrameCount(void);

static unsigned counters[VITA_WAIT_SITES];
static SceUID watchdog = -1;

static const char *const siteNames[VITA_WAIT_SITES] = {
	"vblank", "sleep", "yield", "join", "qsend", "qrecv", "mutex", "spin", "card"
};

/* ---------------------------------------------------------------- the heap's free lists
 *
 * newlib keeps its free-list heads in one static array, __malloc_av_. From index 2 on -- the first
 * two words are padding newlib never uses and leaves zero -- every entry is non-null from the moment
 * the program is loaded: a bin with nothing in it points at itself, and a bin with something in it
 * points at a chunk. A null in there is therefore proof that something
 * wrote where it should not have, and it is worth knowing about immediately: the crash it causes
 * happens inside malloc, at whatever unrelated moment the allocator next walks that bin, with
 * nothing in the log to say when the damage was done or what the game was doing at the time.
 *
 * Checking is 258 loads, which at thirty frames a second is not worth measuring, so the frame driver
 * does it every frame.
 */
extern void *__malloc_av_[];
#define MALLOC_BINS (128 * 2 + 2)

static int heapReported;

void VitaNativeHeapCheck(unsigned frame)
{
	if (heapReported)
		return;
	for (unsigned i = 2; i < MALLOC_BINS; i++) {
		if (__malloc_av_[i])
			continue;
		heapReported = 1;
		VitaNativeMemLog("[HEAP] malloc free-list head %u became null at frame %u: "
		                 "something has written into libc's static data", i, frame);
		/* The shape of the damage says what did it: one null is a stray pointer write, a run of
		 * them is a copy or a fill that went past its end. */
		for (unsigned j = i < 4 ? 0 : i - 4; j < MALLOC_BINS && j < i + 12; j += 4)
			VitaNativeMemLog("[HEAP] av_[%u] = %p %p %p %p", j, __malloc_av_[j],
			                 j + 1 < MALLOC_BINS ? __malloc_av_[j + 1] : NULL,
			                 j + 2 < MALLOC_BINS ? __malloc_av_[j + 2] : NULL,
			                 j + 3 < MALLOC_BINS ? __malloc_av_[j + 3] : NULL);
		return;
	}
}

void VitaOS_WaitTick(enum VitaWaitSite site)
{
	if ((unsigned)site < VITA_WAIT_SITES)
		counters[site]++;
}

/* "vblank=1234(+0) sleep=..." -- the total, and what it moved by since the last report. */
static void FormatCounters(char *out, unsigned length, const unsigned *previous)
{
	unsigned used = 0;

	for (unsigned i = 0; i < VITA_WAIT_SITES && used + 1 < length; i++) {
		int written = snprintf(out + used, length - used, "%s%s=%u(+%u)", used ? " " : "",
		                       siteNames[i], counters[i], counters[i] - previous[i]);
		if (written < 0)
			break;
		used += (unsigned)written;
	}
	out[length - 1] = '\0';
}

/* What the kernel says a thread is doing. The status is a set of bits and not an enumeration -- and
 * psp2's names overlap, WAITING and SUSPEND being the same bit -- so it is decoded a bit at a time
 * and anything left over is printed as a number, which is still more use than nothing. */
static const char *ThreadStatus(SceUID id, char *scratch, unsigned length)
{
	static const struct { unsigned bit; const char *name; } bits[] = {
		{ 1, "running" }, { 2, "ready" }, { 4, "standby" }, { 8, "waiting" },
		{ 16, "dormant" }, { 32, "deleted" }, { 64, "dead" }, { 128, "stagnant" },
		{ 256, "suspended" },
	};
	SceKernelThreadInfo info;
	unsigned status, used = 0;

	memset(&info, 0, sizeof info);
	info.size = sizeof info;
	if (sceKernelGetThreadInfo(id, &info) < 0)
		return "gone";
	status = (unsigned)info.status;
	for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++)
		if (status & bits[i].bit) {
			int written = snprintf(scratch + used, length - used, "%s%s", used ? "|" : "",
			                       bits[i].name);
			if (written < 0)
				break;
			used += (unsigned)written;
			status &= ~bits[i].bit;
			if (used + 1 >= length)
				break;
		}
	if (status && used + 1 < length)
		snprintf(scratch + used, length - used, "%s%x", used ? "|" : "", status);
	else if (!used)
		snprintf(scratch, length, "0");
	scratch[length - 1] = '\0';
	return scratch;
}

static void Report(unsigned frames, unsigned seconds, const unsigned *previous)
{
	char counts[256], scratch[64];
	SceUID owner = -1, ids[MAX_REPORTED];
	unsigned depth = 0, count;

	FormatCounters(counts, sizeof counts, previous);
	VitaOS_LockState(&owner, &depth);
	VitaNativeMemLog("[STALL] no frame for %us at frame %u; lock owner=0x%08x depth=%u",
	                 seconds, frames, (unsigned)owner, depth);
	VitaNativeMemLog("[STALL] waits %s", counts);

	count = VitaOS_ThreadIds(ids, MAX_REPORTED);
	for (unsigned i = 0; i < count; i++)
		VitaNativeMemLog("[STALL] ds thread %u id=0x%08x %s%s", i, (unsigned)ids[i],
		                 ThreadStatus(ids[i], scratch, sizeof scratch),
		                 ids[i] == owner ? " (holds the lock)" : "");
}

static int WatchdogThread(SceSize args, void *argp)
{
	unsigned lastFrames = 0, previous[VITA_WAIT_SITES];
	unsigned long long lastMoved;
	unsigned reportedAt = 0;

	(void)args;
	(void)argp;
	memset(previous, 0, sizeof previous);
	lastFrames = VitaNativeFrameCount();
	lastMoved = VitaOS_Now();

	for (;;) {
		unsigned frames, seconds;

		sceKernelDelayThread(POLL_US);
		frames = VitaNativeFrameCount();
		if (frames != lastFrames) {
			lastFrames = frames;
			lastMoved = VitaOS_Now();
			reportedAt = 0;
			memcpy(previous, counters, sizeof previous);
			continue;
		}
		seconds = (unsigned)((VitaOS_Now() - lastMoved) / 1000000ull);
		if (seconds < STALL_SECONDS)
			continue;
		/* One report when the stall is first believed in, then one every REPEAT_SECONDS. The
		 * counter deltas are against the previous report, so each line says what moved since. */
		if (reportedAt && seconds - reportedAt < REPEAT_SECONDS)
			continue;
		Report(frames, seconds, previous);
		reportedAt = seconds;
		memcpy(previous, counters, sizeof previous);
	}
	return 0;
}

void VitaNativeWatchdogStart(void)
{
	if (watchdog >= 0)
		return;
	/* 0xBF is the lowest priority a user thread may have on this console, and one past it -- the
	 * obvious "below everything" number -- is rejected with SCE_KERNEL_ERROR_ILLEGAL_PRIORITY. The
	 * DS threads live in 0x60 to 0x7F (see os_thread.c), so this is below all of them: the watchdog
	 * must never be the reason a frame is late, and a stalled port is not using its priority to
	 * make progress anyway, so it will get a turn regardless.
	 *
	 * A failure here is logged rather than swallowed. A watchdog that silently did not start is
	 * worse than none: the log then says nothing about a stall, and the absence reads as "it did
	 * not stall". */
	watchdog = sceKernelCreateThread("vitapoke_watchdog", WatchdogThread, 0xBF, 8192, 0,
	                                 SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
	if (watchdog < 0) {
		VitaNativeMemLog("[WATCHDOG] could not start: %08x -- a stall will not be reported",
		                 (unsigned)watchdog);
		return;
	}
	if (sceKernelStartThread(watchdog, 0, NULL) < 0)
		VitaNativeMemLog("[WATCHDOG] created but would not start -- a stall will not be reported");
}
