/* vitapoke: the DS sound engine's output, on the Vita's speakers.
 *
 * port/native-audio-sound mixes the DS's 16 channels into interleaved 16-bit stereo at the DS's own
 * rate: one sample every 1048 ticks of the DS sound clock, 16756991/1048 = 15989.5 Hz. This hands
 * that to sceAudioOut on a port opened at 16000 Hz, so the console's audio hardware does the
 * conversion to its own rate and nothing is resampled in software. The pitch error is
 * 15989.5/16000 = -0.066%, about a hundredth of a semitone.
 *
 * The PSP build reached the same arrangement through sceAudioSRC. This is not that code translated:
 * sceAudioOut is a port with a fixed buffer length rather than a sample-rate-converting channel, and
 * it blocks in the same shape, so the ring buffer and the consumer thread below are the parts worth
 * keeping and the rest is different.
 *
 * Failure is tolerated by design, because silence is better than not booting: if the port cannot be
 * opened or the thread cannot be created, the game runs with no sound and the reason goes to the log.
 *
 * The buffers are static. The port's heap is measured, and a fixed 36 KB of BSS is easier to reason
 * about than an allocation made while the game is loading.
 */
#include <string.h>

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#include "include/vita_os.h"

#define OUT_FREQ    16000
#define OUT_CHUNK   256               /* samples per sceAudioOut call: 16 ms at 16 kHz, a multiple of 64 */
#define RING_FRAMES 8192              /* power of two: 512 ms of slack */
#define RING_MASK   (RING_FRAMES - 1)

/* What the producer aims to keep buffered.
 *
 * The producer is the game thread, once a frame: it delivers about 500 samples in a burst every
 * 33 ms. The consumer takes 256 every 16 ms, steadily. Two frames of slack was not enough to ride
 * out the jitter between those two rhythms -- hardware logs showed the ring reaching zero and better
 * than half of the output chunks coming out part silent -- so this is four frames. The cost is
 * 128 ms between a sound being mixed and being heard, which for this game is not noticeable; the
 * benefit is that a frame arriving late no longer leaves a hole in the sound. */
#define TARGET_FILL 2048

extern int VitaNativeSoundSilent;      /* port/native-audio-sound/sim_audio.cpp: 1 = nothing is mixed */

static short ring[RING_FRAMES * 2];
static short chunk[OUT_CHUNK * 2] __attribute__((aligned(64)));
static volatile unsigned writeIndex, readIndex;
static SceUID thread = -1;
static int port = -1;
static volatile int running;
static int ready;
static unsigned underruns, dropped, written;
/* The lowest the ring got since the last report, and how many times the shortfall correction had
 * nothing left to correct. A single `fill` reading says where the ring was at that instant; this
 * says how close it came to running out in between, which is what a listener actually hears. */
static unsigned lowFill = ~0u;

unsigned VitaNativeAudioOutFill(void) { return (writeIndex - readIndex) & RING_MASK; }
int VitaNativeAudioOutReady(void) { return ready; }
unsigned VitaNativeAudioOutTargetFill(void) { return TARGET_FILL; }
unsigned VitaNativeAudioOutCapacity(void) { return RING_FRAMES; }

/* The consumer. sceAudioOutOutput blocks until the previous buffer has been played, which is what
 * paces this thread; it never takes the DS execution lock, so it is not a DS thread and does not
 * have to give anything up to block.
 */
static int AudioThread(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	while (running) {
		unsigned read, avail, take, first;

		/* Sound off: nothing is being mixed, so do not push silence at a 16 ms cadence. On the
		 * PSP that cost about two frames a second for nothing at all. Park instead. */
		if (VitaNativeSoundSilent) {
			readIndex = writeIndex;   /* drop anything stale so the restart is clean */
			sceKernelDelayThread(50 * 1000);
			continue;
		}
		read = readIndex;
		avail = (writeIndex - read) & RING_MASK;
		if (avail < lowFill)
			lowFill = avail;
		take = avail < OUT_CHUNK ? avail : OUT_CHUNK;
		if (take < OUT_CHUNK)
			underruns++;
		if (take) {
			first = RING_FRAMES - (read & RING_MASK);
			if (first > take)
				first = take;
			memcpy(chunk, ring + (read & RING_MASK) * 2, first * 4);
			if (take > first)
				memcpy(chunk + first * 2, ring, (take - first) * 4);
			readIndex = (read + take) & RING_MASK;
		}
		if (take < OUT_CHUNK)
			memset(chunk + take * 2, 0, (OUT_CHUNK - take) * 4);
		sceAudioOutOutput(port, chunk);
	}
	return 0;
}

int VitaNativeAudioOutInit(void)
{
	int volume[2] = { SCE_AUDIO_OUT_MAX_VOL, SCE_AUDIO_OUT_MAX_VOL };

	if (ready)
		return 0;
	/* BGM rather than MAIN: the console then mixes this with its own sounds and honours the
	 * volume buttons, as a game is expected to. */
	port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, OUT_CHUNK, OUT_FREQ,
	                           SCE_AUDIO_OUT_MODE_STEREO);
	if (port < 0) {
		VitaNativeMemLog("[AUDIO] sceAudioOutOpenPort failed %08x - running silent", port);
		port = -1;
		return -1;
	}
	sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, volume);
	running = 1;
	/* Above the game thread's priority: this one only ever copies a buffer and then blocks, and it
	 * must not be kept off a core by the game, or the audio hardware runs dry. */
	thread = sceKernelCreateThread("vitapoke_audio", AudioThread, 0x40, 0x4000, 0, 0, NULL);
	if (thread < 0) {
		VitaNativeMemLog("[AUDIO] audio thread creation failed %08x - running silent", (int)thread);
		running = 0;
		sceAudioOutReleasePort(port);
		port = -1;
		return -1;
	}
	sceKernelStartThread(thread, 0, NULL);
	ready = 1;
	VitaNativeMemLog("[AUDIO] output ready: %d Hz, %d-sample buffers, %d-sample ring",
	                OUT_FREQ, OUT_CHUNK, RING_FRAMES);
	return 0;
}

/* The producer, called from the game thread. Never blocks: it drops samples on overrun rather than
 * letting a stalled consumer stall the game. */
void VitaNativeAudioOutWrite(const short *interleaved, unsigned frames)
{
	unsigned write = writeIndex;
	unsigned space = RING_MASK - ((write - readIndex) & RING_MASK);
	unsigned first;

	if (frames > space) {
		dropped += frames - space;
		frames = space;
	}
	if (!frames)
		return;
	first = RING_FRAMES - write;
	if (first > frames)
		first = frames;
	memcpy(ring + write * 2, interleaved, first * 4);
	if (frames > first)
		memcpy(ring, interleaved + first * 2, (frames - first) * 4);
	writeIndex = (write + frames) & RING_MASK;
	written += frames;
}

void VitaNativeAudioOutStats(unsigned *fill, unsigned *underrunCount, unsigned *droppedFrames,
                            unsigned *writtenFrames)
{
	if (fill)
		*fill = VitaNativeAudioOutFill();
	if (underrunCount)
		*underrunCount = underruns;
	if (droppedFrames)
		*droppedFrames = dropped;
	if (writtenFrames)
		*writtenFrames = written;
}

/* Read and reset, so each report covers the interval since the last one rather than the whole run. */
unsigned VitaNativeAudioOutLowFill(void)
{
	unsigned low = lowFill;

	lowFill = ~0u;
	return low == ~0u ? 0u : low;
}

void VitaNativeAudioOutShutdown(void)
{
	if (!ready)
		return;
	ready = 0;
	running = 0;
	if (thread >= 0) {
		sceKernelWaitThreadEnd(thread, NULL, NULL);
		sceKernelDeleteThread(thread);
		thread = -1;
	}
	if (port >= 0) {
		sceAudioOutReleasePort(port);
		port = -1;
	}
}

/* Built only for verification runs, where there is no audio device to listen to: raw interleaved
 * 16-bit stereo, written next to the log. The shipping build has none of this. */
#ifdef VITAPOKE_AUDIO_DUMP
#include <stdio.h>
#include "include/vitapoke.h"
#define DUMP_FRAMES 4096
static short dumpBuffer[DUMP_FRAMES * 2];
static unsigned dumpUsed, dumpTotal;

static void DumpFlush(void)
{
	FILE *f;

	if (!dumpUsed)
		return;
	f = fopen(VITAPOKE_DATA_DIR "/audio.raw", dumpTotal ? "ab" : "wb");
	if (f) {
		fwrite(dumpBuffer, 4, dumpUsed, f);
		fclose(f);
		dumpTotal += dumpUsed;
	}
	dumpUsed = 0;
}

void VitaNativeAudioDump(const short *interleaved, unsigned frames)
{
	while (frames) {
		unsigned room = DUMP_FRAMES - dumpUsed;
		unsigned take = frames < room ? frames : room;
		memcpy(dumpBuffer + dumpUsed * 2, interleaved, take * 4);
		dumpUsed += take;
		interleaved += take * 2;
		frames -= take;
		if (dumpUsed == DUMP_FRAMES)
			DumpFlush();
	}
}

unsigned VitaNativeAudioDumpFrames(void) { return dumpTotal + dumpUsed; }
void VitaNativeAudioDumpFlush(void) { DumpFlush(); }
#endif
