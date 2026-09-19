/* vitapoke: the frame, from the game's point of view.
 *
 * The DS game's main loop does not know about frames as this port needs them: it runs its update,
 * waits for vertical blank twice, and repeats. VitaNativeFrameComplete is called once per update --
 * the build injects the call into the decompilation's main.c, right after the game's post-vblank
 * tasks -- and that is where this port does the things the DS hardware did continuously:
 *
 *   - advance the sound engine by however long the frame really took
 *   - hand the frame's input to the renderer
 *   - compose both DS screens and put them on the display
 *   - open the next frame
 *
 * Sound is paced on measured time rather than a nominal 1/30 s on purpose. The frame rate is not
 * exactly 30 anywhere, and a sound engine advanced by a nominal frame drifts against the audio
 * hardware until the buffer starves. The measurement is clamped so that a long load cannot turn
 * into a burst of catch-up.
 */
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "include/vita_os.h"
#include "include/vita_input.h"

extern int VitaNativeRenderInit(void);
extern int VitaNativeRenderBegin(void);
extern int VitaNativeRenderPresentNoWait(void);
extern void VitaNativeRenderSetInput(unsigned keys, int touchMode, int down, int x, int y);
extern void VitaNativeRenderGetTimings(unsigned *presentUs, unsigned *software2DUs);
extern unsigned VitaNativeRenderFrameCount(void);
extern unsigned RenderStage(unsigned stage);
extern void VitaNativeRenderComposedTake(unsigned *top, unsigned *bottom);
extern unsigned VitaNativeG3Polygons(void);
extern void VitaNativeG3TextureStats(unsigned *entries, unsigned *bytes, unsigned *binds,
                                     unsigned *hits, unsigned *decodes, unsigned *evictions);
extern void VitaNativeSoundAdvance(unsigned elapsedMicroseconds);
extern void VitaNativeSoundOutputLine(char *buffer, unsigned length);
extern void VitaNativeVBlankFrameComplete(void);
extern unsigned long long VitaNativeVBlankIdleTake(void);
extern unsigned VitaNativeVBlankWaitsTake(void);
extern void VitaNativeMemReport(const char *tag);
extern void VitaNativeMemPoll(void);

/* One report every 600 frames: twenty seconds of play at 30 fps. Everything in it is a counter the
 * renderer or the sound backend already keeps, so a frame costs a few additions. Build with
 * -DVITAPOKE_REPORT_FRAMES=60 to watch a scene change as it happens, at the cost of a log that grows
 * ten times as fast. */
#ifdef VITAPOKE_REPORT_FRAMES
#define REPORT_FRAMES VITAPOKE_REPORT_FRAMES
#else
#define REPORT_FRAMES 600
#endif

/* How many frames get a line of their own at startup. */
#define FIRST_FRAMES 10

static unsigned frames;

/* For the watchdog, which has no other way to tell a stalled port from a slow one. */
unsigned VitaNativeFrameCount(void) { return frames; }

static unsigned long long frameStart, lastComplete;
static unsigned long long gameUs, audioUs, renderUs, idleUs;
/* The render phase's parts, accumulated like the rest. They used to be printed straight from the
 * renderer, which meant the report mixed true averages with whatever the last frame happened to
 * cost -- and the last frame of a period is often a cheap one, so the expensive stage looked free. */
static unsigned long long accBindUs, accComposeUs, accUploadUs, accPresentUs, accWaitUs;
/* Vertical blanks the game asked to wait for, per update. Two means it is a 30 fps design and 30 is
 * its own rate rather than this port's limit; one with a 33 ms frame would mean the opposite. */
static unsigned long long vblankWaits;

void VitaNativeFrameInit(void)
{
	int result = VitaNativeRenderInit();

	if (result) {
		VitaNativeMemLog("[FRAME] the renderer would not start: %d", result);
		VitaNativeFatal("renderer initialisation failed");
	}
	if (VitaNativeRenderBegin())
		VitaNativeFatal("the renderer refused the first frame");
	frameStart = VitaOS_Now();
	VitaNativeMemReport("frame init");
}

static void Report(void)
{
	unsigned long long window = VitaOS_Now() - frameStart;
	unsigned presentUs = 0, software2DUs = 0;
	unsigned entries = 0, bytes = 0, binds = 0, hits = 0, decodes = 0, evictions = 0;
	/* Frames on which each screen was actually composed. Equal to the period means the frame caches
	 * never hit; far below it means they are doing their job. */
	unsigned composedTop = 0, composedBottom = 0;
	char sound[192];

	VitaNativeRenderGetTimings(&presentUs, &software2DUs);
	VitaNativeRenderComposedTake(&composedTop, &composedBottom);
	VitaNativeG3TextureStats(&entries, &bytes, &binds, &hits, &decodes, &evictions);
	VitaNativeMemLog("[PERF] frames=%u fps=%.2f game_us=%llu idle_us=%llu audio_us=%llu "
	                 "render_us=%llu bind_us=%llu compose_us=%llu upload_us=%llu present_us=%llu "
	                 "wait_us=%llu other_us=%llu "
	                 "vblanks=%u.%02u composed=%u/%u polygons=%u tex=%u/%u binds=%u hits=%u decodes=%u evictions=%u",
	                 frames, window ? REPORT_FRAMES * 1000000.0 / window : 0.0,
	                 gameUs / REPORT_FRAMES, idleUs / REPORT_FRAMES, audioUs / REPORT_FRAMES,
	                 renderUs / REPORT_FRAMES, accBindUs / REPORT_FRAMES,
	                 accComposeUs / REPORT_FRAMES, accUploadUs / REPORT_FRAMES,
	                 accPresentUs / REPORT_FRAMES, accWaitUs / REPORT_FRAMES,
	                 /* What is in the render phase besides those four: the 3D readback, the input
	                  * hand-off, and whatever else grows there later. It was 3.8 ms a frame on
	                  * hardware with no 3D on screen at all, which is why it is printed. Clamped at
	                  * zero: the parts are sampled at slightly different moments from the whole. */
	                 renderUs > accUploadUs + accPresentUs + accWaitUs
	                     ? (renderUs - accUploadUs - accPresentUs - accWaitUs) / REPORT_FRAMES
	                     : 0ULL,
	                 (unsigned)(vblankWaits / REPORT_FRAMES),
	                 (unsigned)((vblankWaits * 100 / REPORT_FRAMES) % 100),
	                 composedTop, composedBottom,
	                 VitaNativeG3Polygons(), entries, bytes, binds, hits, decodes, evictions);
	sound[0] = 0;
	VitaNativeSoundOutputLine(sound, sizeof sound);
	if (sound[0])
		VitaNativeMemLog("%s", sound);
	gameUs = audioUs = renderUs = idleUs = 0;
	accBindUs = accComposeUs = accUploadUs = accPresentUs = accWaitUs = 0;
	vblankWaits = 0;
	frameStart = VitaOS_Now();
}

void VitaNativeFrameComplete(void)
{
	unsigned long long now = VitaOS_Now(), afterAudio;
	unsigned keys;
	int down, x, y;
	int result;

	if (lastComplete)
		gameUs += now - lastComplete;
	idleUs += VitaNativeVBlankIdleTake();
	vblankWaits += VitaNativeVBlankWaitsTake();

	/* How long the frame really took, clamped: below 4 ms is not a frame, and above 66 ms is a
	 * load screen rather than a frame the sound engine should try to catch up with. */
	{
		unsigned long long elapsed = lastComplete ? now - lastComplete : 33333ULL;
		if (elapsed < 4000ULL)
			elapsed = 4000ULL;
		else if (elapsed > 66666ULL)
			elapsed = 66666ULL;
		VitaNativeSoundAdvance((unsigned)elapsed);
	}
	afterAudio = VitaOS_Now();
	audioUs += afterAudio - now;

	/* There is no stylus mode on the Vita -- the touch screen is touched -- so the renderer is
	 * told 0 for it and draws no cursor. */
	VitaNativeInputGetRenderState(&keys, &down, &x, &y);
	VitaNativeRenderSetInput(keys, 0, down, x, y);

	result = VitaNativeRenderPresentNoWait();
	if (result) {
		VitaNativeMemLog("[FRAME] the renderer could not present this frame: %d", result);
		VitaNativeFatal("unsupported renderer state");
	}
	renderUs += VitaOS_Now() - afterAudio;
	accBindUs += RenderStage(0);
	accComposeUs += RenderStage(1);
	accUploadUs += RenderStage(3);
	accPresentUs += RenderStage(2);
	accWaitUs += RenderStage(4);
	frames++;

	/* The first frames, one line each. A port that reaches the game's first scene and then appears
	 * to stop is either slow or stuck, and the only way to tell from a log is to have the first few
	 * frames in it with their timings. Cheap: it stops after FIRST_FRAMES. */
	if (frames <= FIRST_FRAMES) {
		VitaNativeMemLog("[FRAME] %u game_us=%llu audio_us=%llu render_us=%llu "
		                 "bind_us=%u compose_us=%u upload_us=%u present_us=%u polygons=%u",
		                 frames, gameUs, audioUs, renderUs, RenderStage(0), RenderStage(1),
		                 RenderStage(3), RenderStage(2), VitaNativeG3Polygons());
	}

	VitaNativeHeapCheck(frames);
#ifdef VITAPOKE_HEAP_GUARD
	/* Guarded builds only: name the call sites holding the most memory, often enough to watch one
	 * of them grow. */
	if (frames % 300 == 0) {
		extern void VitaNativeHeapGuardReport(void);
		VitaNativeHeapGuardReport();
	}
#endif
	if (frames % 10 == 0)
		VitaNativeMemPoll();
	if (frames % REPORT_FRAMES == 0)
		Report();

	VitaNativeVBlankFrameComplete();
	if (VitaNativeRenderBegin())
		VitaNativeFatal("the renderer refused a frame");
	lastComplete = VitaOS_Now();
}
