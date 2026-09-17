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
static int ready;

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

void VitaNativeVBlankReset(void) { ready = 0; }

/* The caller (os_core.c's OS_WaitIrq) has already dropped the DS execution lock, so other DS threads
 * run while this waits -- which is what the DS does during vertical blank. */
void VitaNativeVBlankWait(void)
{
	unsigned long long before = VitaOS_Now();

	waits++;
	ready = 1;
	sceDisplayWaitVblankStart();
	idleUs += VitaOS_Now() - before;
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
