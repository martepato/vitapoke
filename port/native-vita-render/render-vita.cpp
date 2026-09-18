/* vitapoke: the DS's two screens, composed and put on the Vita's display.
 *
 * The DS draws its screens with hardware the Vita does not have: two 2D engines, each compositing
 * four tiled backgrounds, 128 sprites, windows, priorities and blending, line by line. This port
 * does that compositing in software -- melonDS's 2D renderer, which is the reference for what the
 * hardware actually does -- and asks the Vita's GPU only to put the two finished 256x192 images on
 * the screen.
 *
 * Software compositing is not a placeholder. The DS's 2D pipeline is per-pixel state that a GPU has
 * to be talked into approximating; the PSP port spent most of its renderer on doing that with tile
 * atlases, and still fell back to software for anything it could not express (affine and bitmap
 * backgrounds, windows, VRAM display mode). The Vita's CPU is a 444 MHz Cortex-A9 with NEON against
 * the PSP's 333 MHz MIPS, and four cores where the PSP had one, so the honest software path that the
 * PSP could only use as a fallback is affordable here as the main one -- and it is exactly right,
 * where a GPU approximation is nearly right.
 *
 * The GPU's part is two textures drawn as quads where port/vita/include/vitapoke.h says, and
 * rasterising the DS's 3D output, which is a triangle list rather than a composite and so cannot be
 * done this way: that is g3_backend.cpp. Everything the GPU does goes through gpu.h.
 *
 * The frame is: VitaNativeRenderBegin before the game draws anything, the game's own update, then
 * VitaNativeRenderPresent -- which finishes the 3D, composes both DS screens from whatever the game
 * left in DS memory, and puts them on the display.
 */
/* ------------------------------------------------------------------ include order matters here
 *
 * libntr's nitro/types.h and melonDS's types.h both define u8..s64. They are the same widths, but
 * nitro spells u32 `unsigned long` while melonDS spells it `uint32_t`, which on this toolchain is
 * `unsigned int` -- the same size, a different type, and a C++ error to declare twice. On the PSP
 * both spellings were `unsigned long` and the two agreed by accident. nitro's win, because the SDK
 * and the whole game are compiled against them; melonDS's header is upstream and kept unmodified,
 * so its include guard is closed by hand instead.
 *
 * There is no GL header here, and there cannot be: see gpu.h.
 */
#include <nitro.h>

#define TYPES_H   /* melonDS's types.h: nitro has already defined these -- see above */

#include "../native-render-opt/native_render.h"
#include "../native-render-opt/native_gpu.h"
#include "../vita/include/vitapoke.h"
#include "gpu.h"

#include <simulator/g3_handler.h>
#include <simulator/g3_draw.h>

#include <cstdio>
#include <cstring>

#include <psp2/kernel/threadmgr.h>

#ifdef VITAPOKE_FRAME_DUMP
#ifndef VITAPOKE_FRAME_DUMP_LAST
#define VITAPOKE_FRAME_DUMP_LAST 600
#endif
#endif

extern "C" void VitaNativeMemLog(const char *, ...);
extern "C" void VitaNativeFatal(const char *);
extern "C" unsigned long long VitaOS_Now(void);
extern "C" void VitaNativeG3Release(void);
extern "C" void VitaNativeG3FrameBegin(void);
extern "C" void VitaNativeG3FrameEnd(int wanted);

/* The panel layout lives in port/vita/include/vitapoke.h, and the GPU layer cannot include that
 * header (it must stay out of libntr's include graph), so it carries its own copy of the numbers.
 * These make the two disagreeing a build failure rather than a misplaced screen. */
static_assert(VITAPOKE_SCREEN_W == 960 && VITAPOKE_SCREEN_H == 544, "gpu.cpp has the display size");
static_assert(VITAPOKE_DS_W == 256 && VITAPOKE_DS_H == 192, "gpu.cpp has the DS screen size");
static_assert(VITAPOKE_MAIN_W == VITAPOKE_SCREEN_W / 2, "gpu.cpp gives each panel half the width");
static_assert(VITAPOKE_MAIN_H == VITAPOKE_MAIN_W * VITAPOKE_DS_H / VITAPOKE_DS_W,
              "gpu.cpp keeps each panel at the DS's aspect");
static_assert(VITAPOKE_MAIN_X == 0 && VITAPOKE_SUB_X == VITAPOKE_SCREEN_W - VITAPOKE_SUB_W,
              "gpu.cpp puts the panels at the outer edges");
static_assert(VITAPOKE_MAIN_X + VITAPOKE_MAIN_W <= VITAPOKE_SUB_X,
              "the panels must not overlap: one would be drawn over the other");

/* The two composed DS screens. 256 columns of stride to keep each row aligned, as the compositor
 * expects, and 256 rows so a texture upload can treat it as a square. */
/* Two pairs of composed screens. The compositor runs on its own thread (see the note at Present)
 * and writes the pair the display is not reading, so the upload never races the composite. */
static u32 rawBuffers[2][2][256 * 256] __attribute__((aligned(64)));
static unsigned rawWrite;                        /* the pair the compositor is filling */
#define raw rawBuffers[rawWrite]

static GPU2D::Unit engineA(0), engineB(1);
static GPU2D::SoftRenderer renderer;

static bool initialized = false, openFrame = false;
static unsigned frames = 0, lastDraw = 0, previousPower = ~0u, grace = 0;
static unsigned inputKeys = 0, oldKeys = 0;
static int touchDown = 0, oldDown = 0, touchX = 128, touchY = 96, oldX = 128, oldY = 96;
static unsigned bindUs = 0, drawUs = 0, uploadUs = 0, presentUs = 0, last2DUs = 0;
/* The frame caches' state; see where they are consulted for what these are for. */
#define CACHE_GIVE_UP 4
#define CACHE_RETRY 64
static unsigned topMisses, bottomMisses, topSkip, bottomSkip;
/* How many times each screen was actually composed, for the frame report. */
static unsigned composedA, composedB;

/* The DS's memory-mapped graphics registers, as the port keeps them: an array the game writes
 * through the SDK, which this reads back to configure the compositor. */
extern "C" unsigned char VitaNative_GfxRegisters[];
static unsigned reg16(unsigned off) { return *(volatile u16 *)(VitaNative_GfxRegisters + off); }
static unsigned reg32(unsigned off) { return *(volatile u32 *)(VitaNative_GfxRegisters + off); }

/* Copy one engine's register block into the compositor. The offsets are the DS's own; the widths
 * matter, because a 32-bit register read as two 16-bit halves loses the high half's meaning. */
static void bindRegisters(GPU2D::Unit &u, unsigned base)
{
	u.Write32(0, reg32(base));
	for (unsigned off = 8; off <= 0x54; off += 2) {
		if (off == 0x28 || off == 0x2c || off == 0x38 || off == 0x3c) {
			u.Write32(off, reg32(base + off));
			off += 2;
		} else {
			u.Write16(off, reg16(base + off));
		}
	}
	u.Write16(0x6c, reg16(base + 0x6c));
}

/* Point the compositor at the DS memory the game has been filling: palettes, OAM, and the VRAM banks
 * with whatever mapping the game has chosen this frame. */
/* Copy the DS's palettes, OAM and VRAM mapping into the shape the compositor reads.
 *
 * dispA and dispB are the two engines' DISPCNT. The extended-palette buffers below are 80 KB of
 * memset and up to 80 KB of copying, and they are only ever read -- and only ever compared by the
 * frame caches -- when the engine that owns them has extended palettes turned on. Doing that work
 * unconditionally cost milliseconds a frame on hardware in scenes that do not use the feature at
 * all, which is most of them. */
static void mapMemory(unsigned dispA, unsigned dispB)
{
	const bool extABg = (dispA & (1u << 30)) != 0, extAObj = (dispA & (1u << 31)) != 0;
	const bool extBBg = (dispB & (1u << 30)) != 0, extBObj = (dispB & (1u << 31)) != 0;

	memcpy(GPU::Palette, s_HW_BG_PLTT, 512);
	memcpy(GPU::Palette + 512, s_HW_OBJ_PLTT, 512);
	memcpy(GPU::Palette + 1024, s_HW_DB_BG_PLTT, 512);
	memcpy(GPU::Palette + 1536, s_HW_DB_OBJ_PLTT, 512);
	memcpy(GPU::OAM, s_HW_OAM, 1024);
	memcpy(GPU::OAM + 1024, s_HW_DB_OAM, 1024);
	GPU::VRAMMap_LCDC = 0;
	for (unsigned i = 0; i < 4; i++) {
		GPU::VRAM[i] = s_HW_LCDC_VRAM + i * 0x20000;
		if ((*(volatile u8 *)(VitaNative_GfxRegisters + 0x240 + i) & 0x87) == 0x80)
			GPU::VRAMMap_LCDC |= 1u << i;
	}
	if (extABg)
		memset(GPU::VRAMFlat_ABGExtPal, 0, 32768);
	if (extBBg)
		memset(GPU::VRAMFlat_BBGExtPal, 0, 32768);
	if (extAObj)
		memset(GPU::VRAMFlat_AOBJExtPal, 0, 8192);
	if (extBObj)
		memset(GPU::VRAMFlat_BOBJExtPal, 0, 8192);
	if (extABg && (*(volatile u8 *)(VitaNative_GfxRegisters + 0x244) & 0x87) == 0x84)
		memcpy(GPU::VRAMFlat_ABGExtPal, s_HW_LCDC_VRAM + 0x80000, 32768);
	if (extABg || extAObj) {
		for (unsigned i = 0; i < 2; i++) {
			unsigned c = *(volatile u8 *)(VitaNative_GfxRegisters + 0x245 + i);
			const u8 *src = s_HW_LCDC_VRAM + 0x90000 + i * 0x4000;
			if (extABg && (c & 0x87) == 0x84)
				memcpy(GPU::VRAMFlat_ABGExtPal + ((c & 8) ? 16384 : 0), src, 16384);
			if (extAObj && (c & 0x87) == 0x85)
				memcpy(GPU::VRAMFlat_AOBJExtPal, src, 8192);
		}
	}
	if (extBBg && (*(volatile u8 *)(VitaNative_GfxRegisters + 0x248) & 0x87) == 0x82)
		memcpy(GPU::VRAMFlat_BBGExtPal, s_HW_LCDC_VRAM + 0x98000, 32768);
	if (extBObj && (*(volatile u8 *)(VitaNative_GfxRegisters + 0x249) & 0x87) == 0x83)
		memcpy(GPU::VRAMFlat_BOBJExtPal, s_HW_LCDC_VRAM + 0xa0000, 8192);
}

/* The frame-reuse caches: they snapshot everything an engine's output depends on, so a screen whose
 * inputs have not changed is not composed again. Written for the PSP, where this was the difference
 * between playable and not; still worth having here, because the work it skips is CPU work. */
#include "../native-render-opt/bottom_cache.h"
#include "../native-render-opt/top_cache.h"

extern "C" void VitaNativeRenderSetInput(unsigned keys, int touchMode, int down, int x, int y)
{
	(void)touchMode;   /* no stylus mode on the Vita: the touch screen is touched */
	inputKeys = keys;
	touchDown = down;
	touchX = x;
	touchY = y;
}

/* The compositor runs on its own thread.
 *
 * A DS update is two vertical blanks, 33 ms, and the game spends nearly all of it asleep waiting for
 * them -- 25 ms of the 31 ms it took on hardware. Composing the two screens took another 10 to 19 ms
 * AFTER that wait, on the same thread, so the port turned a 33 ms budget into 50 and the game ran at
 * two thirds speed with a core idle throughout.
 *
 * So the composite is handed to a worker and the game thread goes straight back to the game. The
 * work lands in the window the game was going to sleep through anyway. What the game thread still
 * does is the GPU: vitaGL's context belongs to the thread that made it, so the upload and the draw
 * stay here, and they are half a millisecond.
 *
 * The cost is one frame of latency -- the screens put up are the ones composed during the previous
 * update -- and tearing: the worker reads the DS's VRAM while the game is free to write it, where
 * before it read a quiescent snapshot. A DS does the same thing, drawing a scanline at a time while
 * the program runs, and the alternative is copying half a megabyte of VRAM per frame to avoid it.
 *
 * The registers are not read that way. Those are snapshotted here, on the game thread, into the job:
 * they are small, and a composite that read half of one frame's DISPCNT and half of the next's would
 * not tear, it would be wrong.
 */
struct FrameJob {
	unsigned power, a, b;
	unsigned buffer;        /* which pair of raw buffers to compose into */
};
static FrameJob job;
static SceUID jobReady = -1, jobDone = -1;
static SceUID worker = -1;
static bool jobOutstanding;      /* a composite has been handed over and not yet collected */
static unsigned composeUsWorker; /* the worker's own timing, read by the game thread when it waits */
/* How long the game thread had to wait for a composite that did not finish in time. Zero means the
 * work was completely hidden behind the game's own update, which is the point of doing it this way. */
static unsigned waitUs = 0;

static void Compose(const FrameJob &j);

static int WorkerMain(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	for (;;) {
		if (sceKernelWaitSema(jobReady, 1, NULL) < 0)
			return 0;
		Compose(job);
		sceKernelSignalSema(jobDone, 1);
	}
}

extern "C" unsigned VitaNativeRenderLastDrawMask() { return lastDraw; }
extern "C" unsigned VitaNativeRenderFrameCount() { return frames; }

extern "C" int VitaNativeRenderInit()
{
	if (initialized)
		return 0;
	frames = 0;
	topCache.valid = false;
	bottomCache.valid = false;
	topMisses = bottomMisses = topSkip = bottomSkip = 0;
	composedA = composedB = 0;
	previousPower = ~0u;
	grace = 0;
	engineA.Reset();
	engineB.Reset();

	if (VitaGpuInit() < 0)
		return -1;

	/* The compositor's thread. Just below the game's own priority, so it runs while the game sleeps
	 * on the vertical blank and never in front of the game when both are runnable. */
	if (worker < 0) {
		jobReady = sceKernelCreateSema("vitapoke_compose_go", 0, 0, 1, NULL);
		jobDone = sceKernelCreateSema("vitapoke_compose_done", 0, 0, 1, NULL);
		worker = sceKernelCreateThread("vitapoke_compose", WorkerMain, 0x60 + 17, 64 * 1024, 0,
		                               SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
		if (jobReady < 0 || jobDone < 0 || worker < 0) {
			VitaNativeMemLog("[RENDER] could not start the compositor thread");
			return -1;
		}
		sceKernelStartThread(worker, 0, NULL);
	}
	jobOutstanding = false;
	job.buffer = 0;

	/* The matrix stacks the DS geometry engine keeps. The game sets them up once and then relies
	 * on them, so they start as identity rather than as whatever the SDK last left behind. */
	G3SIM_MtxMode(GX_MTXMODE_TEXTURE);
	G3SIM_Identity();
	G3SIM_MtxMode(GX_MTXMODE_PROJECTION);
	G3SIM_Identity();
	G3SIM_MtxMode(GX_MTXMODE_POSITION_VECTOR);
	G3SIM_Identity();

	initialized = true;
	VitaNativeMemLog("[RENDER] ready: software 2D, two %dx%d panels drawn %dx%d side by side "
	                "on a %dx%d display",
	                VITAPOKE_DS_W, VITAPOKE_DS_H, VITAPOKE_MAIN_W, VITAPOKE_MAIN_H,
	                VITAPOKE_SCREEN_W, VITAPOKE_SCREEN_H);
	return 0;
}

extern "C" int VitaNativeRenderBegin()
{
	if (!initialized || openFrame)
		return -1;
	openFrame = true;
	/* Everything the game draws in 3D this frame goes into the 3D backend's own render target. */
	VitaNativeG3FrameBegin();
	return 0;
}

/* Compose one frame's two screens into its pair of buffers. Runs on the worker; touches no GPU. */
static void Compose(const FrameJob &j)
{
	const unsigned power = j.power, a = j.a, b = j.b;
	unsigned long long phase = VitaOS_Now();
	(void)b;
	rawWrite = j.buffer;

	/* Display capture and the main-memory FIFO display mode are DS features this port does not
	 * implement. The game does not use them; if it ever did, the frame would be wrong, so say so
	 * rather than draw something plausible. */
	if ((reg32(0x64) & 0x80000000u) || ((a >> 16) & 3) == 3) {
		VitaNativeMemLog("[RENDER] unsupported display capture or main-memory FIFO mode");
		return -2;
	}

	/* Engine B (the touch screen) holds still for long stretches -- a menu, a map, the party. It is
	 * composed every third frame unless something the player did might have changed it, which is
	 * what `grace` is: 30 frames of attention after any input. Engine A, which is usually the
	 * animated one, is composed whenever its inputs differ. */
	bool changed = inputKeys != oldKeys || touchDown != oldDown || touchX != oldX ||
	               touchY != oldY || power != previousPower;
	if (changed || touchDown)
		grace = 30;
	else if (grace)
		grace--;
	bool drawB = frames == 0 || !(power & 0x8000) || grace || frames % 3 == 0;
	bool mappingChanged = ((power ^ previousPower) & 0x8000) != 0;
	previousPower = power;
	oldKeys = inputKeys;
	oldDown = touchDown;
	oldX = touchX;
	oldY = touchY;

	mapMemory(a, b);

	/* Whether to ask the frame caches at all.
	 *
	 * A cache hit skips composing a screen, which is the most expensive thing here. A cache miss
	 * costs the comparison that discovered it -- up to 800 KB of VRAM for the top screen -- and then
	 * the same again to refresh the snapshot, and it buys nothing. In a menu that is a fine trade,
	 * because almost every frame hits. In an animated scene nothing ever matches and the whole cost
	 * is waste: hardware showed it at nearly 6 ms a frame, a third of the render budget, on frames
	 * that then composed anyway.
	 *
	 * So the caches are asked until they have missed CACHE_GIVE_UP times in a row, then left alone
	 * for CACHE_RETRY frames before being given another chance. A scene that settles gets its cache
	 * back within half a second; a scene that never settles pays the comparison on one frame in
	 * sixty-four instead of every one. */
	if (topSkip)
		topSkip--;
	if (bottomSkip)
		bottomSkip--;
	const bool consultTop = topSkip == 0;
	const bool consultBottom = bottomSkip == 0;

	bool drawA = mappingChanged || !consultTop || !TopSame(power, a);
	if (drawB && !mappingChanged && consultBottom && (power & 0x8000) && BottomSame(power))
		drawB = false;
	if (!drawA)
		topMisses = 0;
	else if (consultTop && ++topMisses >= CACHE_GIVE_UP) {
		topMisses = 0;
		topSkip = CACHE_RETRY;
	}
	if (!drawB)
		bottomMisses = 0;
	else if (consultBottom && ++bottomMisses >= CACHE_GIVE_UP) {
		bottomMisses = 0;
		bottomSkip = CACHE_RETRY;
	}
	if (drawA)
		composedA++;
	if (drawB)
		composedB++;
	engineA.Enabled = (power & 2) != 0;
	engineB.Enabled = (power & 512) != 0;
	bindRegisters(engineA, 0);
	bindRegisters(engineB, 0x1000);
	bindUs = (unsigned)(VitaOS_Now() - phase);

	phase = VitaOS_Now();
	FrameStart(engineA);
	FrameStart(engineB);
	renderer.SetFramebuffer(raw[0], raw[1]);
	for (unsigned line = 0; line < 192; line++) {
		GPU::VCount = line;
		if (drawA) {
			engineA.CheckWindows(line);
			renderer.DrawSprites(line, &engineA);
			renderer.DrawScanline(line, &engineA);
		}
		if (drawB) {
			engineB.CheckWindows(line);
			renderer.DrawSprites(line, &engineB);
			renderer.DrawScanline(line, &engineB);
		}
	}
	/* Only refresh a snapshot that something is going to compare against: while a cache is being
	 * left alone, copying into it would be the other half of the cost this is avoiding. */
	if (drawA && consultTop)
		SaveTop(power, a);
	if (drawB && consultBottom)
		SaveBottom(power);
	lastDraw = (drawA ? 1u : 0u) | (drawB ? 2u : 0u);
	drawUs = (unsigned)(VitaOS_Now() - phase);
	last2DUs = bindUs + drawUs;

#ifdef VITAPOKE_FRAME_DUMP
	/* Verification builds only. The composed screens, as they are, written to the memory card: the
	 * one way to see what this renderer produced without watching the console. Both panels go into
	 * one file, 256x192 each, R,G,B,A bytes, top screen first; tests/vita/raw_to_png.py turns them
	 * into pictures. VITAPOKE_FRAME_DUMP is the interval in frames, and VITAPOKE_FRAME_DUMP_LAST the
	 * frame to stop at, because a card fills up quickly at 384 KB a frame. */
	if (frames % (VITAPOKE_FRAME_DUMP) == 0 && frames <= (VITAPOKE_FRAME_DUMP_LAST)) {
		char path[80];
		FILE *out;

		snprintf(path, sizeof path, VITAPOKE_DATA_DIR "/frame%05u.raw", frames);
		out = fopen(path, "wb");
		if (out) {
			/* Row by row, because the buffer's stride is 256 pixels and only 192 rows are used. */
			for (unsigned e = 0; e < 2; e++) {
				unsigned engine = (power & 0x8000) ? e : 1 - e;
				for (unsigned row = 0; row < VITAPOKE_DS_H; row++)
					fwrite(raw[engine] + row * 256, 4, VITAPOKE_DS_W, out);
			}
			fclose(out);
		}
	}
#endif


	composeUsWorker = bindUs + drawUs;
}

static int Present()
{
	if (!openFrame)
		return -1;
	openFrame = false;

	unsigned long long phase;

	/* Collect the previous frame's composite before touching its buffers. This is the only place
	 * the game thread can block on the worker, and it only does so when a composite took longer
	 * than the update it was hidden behind. */
	if (jobOutstanding) {
		phase = VitaOS_Now();
		sceKernelWaitSema(jobDone, 1, NULL);
		waitUs = (unsigned)(VitaOS_Now() - phase);
		jobOutstanding = false;

		phase = VitaOS_Now();
		for (unsigned e = 0; e < 2; e++)
			if (lastDraw & (1u << e))
				VitaGpuPanelUpload((int)e, rawBuffers[job.buffer][e]);
		uploadUs = (unsigned)(VitaOS_Now() - phase);

		phase = VitaOS_Now();
		/* POWCNT1 bit 15 says which engine is on the physical top screen. */
		VitaGpuPresent((job.power & 0x8000) != 0);
		presentUs = (unsigned)(VitaOS_Now() - phase);
	} else {
		waitUs = uploadUs = presentUs = 0;
	}

	/* Hand the next one over. The registers are read here, on this thread, so the worker sees one
	 * frame's worth of them. */
	job.power = reg16(0x304);
	job.a = reg32(0);
	job.b = reg32(0x1000);

	/* Finish the 3D and read it back, but only when engine A is actually showing it: the display
	 * has to be on, in its normal 2D mode, with BG0 enabled and taking its content from the 3D
	 * engine. Outside the field and battle that is not the case, and then there is no reason to
	 * make the CPU wait for the GPU at all.
	 *
	 * On this thread, before the hand-off, for two reasons: it is a GPU call, and the composite
	 * about to start reads what it produces. The previous composite has already been collected
	 * above, so nothing is reading the readback buffer while it is refilled. */
	VitaNativeG3FrameEnd((job.power & 2) && ((job.a >> 16) & 3) == 1 && (job.a & (1 << 8))
	                     && (job.a & (1 << 3)));

	job.buffer ^= 1u;
	jobOutstanding = true;
	sceKernelSignalSema(jobReady, 1);

	frames++;
	return 0;
}

extern "C" int VitaNativeRenderPresent() { return Present(); }
extern "C" int VitaNativeRenderPresentNoWait() { return Present(); }

extern "C" void VitaNativeRenderShutdown()
{
	if (!initialized)
		return;
	/* Let a composite in flight finish before anything it reads goes away. It cannot be abandoned:
	 * the worker is inside the DS's VRAM and the 2D engines' state, not in anything interruptible. */
	if (jobOutstanding) {
		sceKernelWaitSema(jobDone, 1, NULL);
		jobOutstanding = false;
	}
	openFrame = false;
	VitaNativeG3Release();
	VitaGpuShutdown();
	initialized = false;
}

extern "C" void VitaNativeRenderGetTimings(unsigned *readbackUs, unsigned *software2DUs)
{
	if (readbackUs)
		*readbackUs = presentUs;
	if (software2DUs)
		*software2DUs = last2DUs;
}

/* How many frames each screen was composed on since the last call, which says whether the frame
 * caches are earning their keep: equal to the frames in the period means they never hit. */
extern "C" void VitaNativeRenderComposedTake(unsigned *top, unsigned *bottom)
{
	if (top)
		*top = composedA;
	if (bottom)
		*bottom = composedB;
	composedA = composedB = 0;
}

/* 0 = binding the DS registers and memory, 1 = compositing, 2 = putting it on the screen,
 * 3 = uploading the composed panels to the GPU, 4 = waiting for the compositor to finish the
 * previous frame, which is zero when its work fitted inside the game's own update. */
extern "C" unsigned RenderStage(unsigned stage)
{
	return stage == 0 ? bindUs : stage == 1 ? drawUs : stage == 3 ? uploadUs
	     : stage == 4 ? waitUs : presentUs;
}

/* What the port has asked the GPU to hold, for the memory report. vitaGL owns its own pools; this is
 * what the renderer has added to them. */
extern "C" void VitaNativeRenderMemStats(unsigned *bytes, unsigned *high, unsigned *blocks)
{
	VitaGpuMemStats(bytes, high, blocks);
}
