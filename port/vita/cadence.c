/* vitapoke: the DS's vertical blank, on the Vita's.
 *
 * The game's clock is its vblank wait: it asks the SDK to wait for vertical blank twice per update,
 * and everything from text speed to animation timing follows from how long that takes. The DS's
 * vblank is 59.83 Hz.
 *
 * The PSP build could not use that console's vblank here, because its renderer already waited for
 * vsync when presenting and the two waits would have compounded; it ran a software 60 Hz deadline
 * clock instead. The Vita's renderer presents without waiting (the game owns its own pacing), so
 * this can be the real thing: sceDisplayWaitVblankStart is the console's 60 Hz vertical blank, which
 * is within 0.3% of the DS's. Using it means the game's timing is driven by the display rather than
 * by a timer that has to be kept honest against it.
 *
 * A frame that overruns simply lands on the next vblank, which is what happens on a DS that misses
 * one. The deadline recovery below only stops a long load from being followed by a burst of
 * catch-up updates.
 */
#include <psp2/display.h>
#include <psp2/kernel/threadmgr.h>

#include "include/vita_os.h"

static unsigned long long idleUs;
static unsigned waits;
static unsigned skipped;
static int ready;
/* The vertical blank this port last handed to the game, as sceDisplayGetVcount counts them. The
 * difference between it and the display's own count is how many blanks passed while the game was
 * working -- blanks it is owed and should not wait for again. */
static unsigned lastVcount;
static int vcountValid;

/* The most a slow frame may bank. A load that takes a second passes sixty blanks and the game must
 * not then run sixty updates back to back to use them up; two is one game update's worth, which is
 * all the credit a 30 fps design can spend before it has to wait again. */
#define VBLANK_CREDIT_MAX 2

/* Microseconds spent waiting for the display rather than working. The frame profiler subtracts it
 * from the game's own budget, so a slow frame can be told from a frame that finished early. */
unsigned long long VitaNativeVBlankIdleTake(void)
{
	unsigned long long value = idleUs;
	idleUs = 0;
	return value;
}

/* Waits per completed game update: a 30 fps game shows two. */
unsigned VitaNativeVBlankWaitsTake(void)
{
	unsigned value = waits;
	waits = 0;
	return value;
}

/* Waits that returned at once because the blank had already gone by. */
unsigned VitaNativeVBlankSkippedTake(void)
{
	unsigned value = skipped;
	skipped = 0;
	return value;
}

void VitaNativeVBlankReset(void)
{
	ready = 0;
	vcountValid = 0;
}

/* The caller (os_core.c's OS_WaitIrq) has already dropped the DS execution lock, so other DS threads
 * run while this waits -- which is what the DS does during vertical blank. */
void VitaNativeVBlankWait(void)
{
	unsigned long long before;
	unsigned now = (unsigned)sceDisplayGetVcount();

	waits++;
	ready = 1;

	if (!vcountValid) {
		lastVcount = now;
		vcountValid = 1;
	} else if ((unsigned)(now - lastVcount) > VBLANK_CREDIT_MAX) {
		/* Further behind than a game update's worth of blanks -- a load, or the frame after one.
		 * Give up the rest rather than run updates back to back to use them. */
		lastVcount = now - VBLANK_CREDIT_MAX;
	}

	if (now != lastVcount) {
		/* This blank has already happened: the game was still working when it went by, and on a DS
		 * the interrupt would have been taken at the time. Waiting for the *next* one is what made a
		 * frame that overran cost two more blanks on top of the overrun -- 24 ms a frame in the
		 * field, a third of the frame, spent waiting for time that had already passed. */
		lastVcount++;
		skipped++;
		return;
	}

	before = VitaOS_Now();
	sceDisplayWaitVblankStart();
	idleUs += VitaOS_Now() - before;
	lastVcount = (unsigned)sceDisplayGetVcount();
}

/* Called once per completed game update. Nothing to catch up on when the display is the clock: the
 * next wait lands on the next vblank whatever happened in this one. It stays as the place the frame
 * driver marks the boundary, because that is where a future pacing policy would go.
 */
void VitaNativeVBlankFrameComplete(void)
{
	if (!ready)
		return;
}
