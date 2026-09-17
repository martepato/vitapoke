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
extern unsigned VitaNativeG3Polygons(void);
extern void VitaNativeG3TextureStats(unsigned *entries, unsigned *bytes, unsigned *binds,
                                     unsigned *hits, unsigned *decodes);
extern void VitaNativeSoundAdvance(unsigned elapsedMicroseconds);
extern void VitaNativeSoundOutputLine(char *buffer, unsigned length);
extern void VitaNativeVBlankFrameComplete(void);
extern unsigned long long VitaNativeVBlankIdleTake(void);
extern void VitaNativeMemReport(const char *tag);
extern void VitaNativeMemPoll(void);

/* One report every 600 frames: twenty seconds of play at 30 fps. Everything in it is a counter the
 * renderer or the sound backend already keeps, so a frame costs a few additions. */
#define REPORT_FRAMES 600

/* How many frames get a line of their own at startup. */
#define FIRST_FRAMES 10

static unsigned frames;
static unsigned long long frameStart, lastComplete;
static unsigned long long gameUs, audioUs, renderUs, idleUs;

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
	unsigned entries = 0, bytes = 0, binds = 0, hits = 0, decodes = 0;
	char sound[192];

	VitaNativeRenderGetTimings(&presentUs, &software2DUs);
	VitaNativeG3TextureStats(&entries, &bytes, &binds, &hits, &decodes);
	VitaNativeMemLog("[PERF] frames=%u fps=%.2f game_us=%llu idle_us=%llu audio_us=%llu "
	                 "render_us=%llu bind_us=%u compose_us=%u present_us=%u "
	                 "polygons=%u tex=%u/%u binds=%u hits=%u decodes=%u",
	                 frames, window ? REPORT_FRAMES * 1000000.0 / window : 0.0,
	                 gameUs / REPORT_FRAMES, idleUs / REPORT_FRAMES, audioUs / REPORT_FRAMES,
	                 renderUs / REPORT_FRAMES, RenderStage(0), RenderStage(1), RenderStage(2),
	                 VitaNativeG3Polygons(), entries, bytes, binds, hits, decodes);
	sound[0] = 0;
	VitaNativeSoundOutputLine(sound, sizeof sound);
	if (sound[0])
		VitaNativeMemLog("%s", sound);
	gameUs = audioUs = renderUs = idleUs = 0;
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
	frames++;

	/* The first frames, one line each. A port that reaches the game's first scene and then appears
	 * to stop is either slow or stuck, and the only way to tell from a log is to have the first few
	 * frames in it with their timings. Cheap: it stops after FIRST_FRAMES. */
	if (frames <= FIRST_FRAMES) {
		VitaNativeMemLog("[FRAME] %u game_us=%llu audio_us=%llu render_us=%llu "
		                 "bind_us=%u compose_us=%u present_us=%u polygons=%u",
		                 frames, gameUs, audioUs, renderUs, RenderStage(0), RenderStage(1),
		                 RenderStage(2), VitaNativeG3Polygons());
	}

	if (frames % 10 == 0)
		VitaNativeMemPoll();
	if (frames % REPORT_FRAMES == 0)
		Report();

	VitaNativeVBlankFrameComplete();
	if (VitaNativeRenderBegin())
		VitaNativeFatal("the renderer refused a frame");
	lastComplete = VitaOS_Now();
}
