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
static u32 raw[2][256 * 256] __attribute__((aligned(64)));

static GPU2D::Unit engineA(0), engineB(1);
static GPU2D::SoftRenderer renderer;

static bool initialized = false, openFrame = false;
static unsigned frames = 0, lastDraw = 0, previousPower = ~0u, grace = 0;
static unsigned inputKeys = 0, oldKeys = 0;
static int touchDown = 0, oldDown = 0, touchX = 128, touchY = 96, oldX = 128, oldY = 96;
static unsigned bindUs = 0, drawUs = 0, uploadUs = 0, presentUs = 0, last2DUs = 0;

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
static void mapMemory()
{
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
	memset(GPU::VRAMFlat_ABGExtPal, 0, 32768);
	memset(GPU::VRAMFlat_BBGExtPal, 0, 32768);
	memset(GPU::VRAMFlat_AOBJExtPal, 0, 8192);
	memset(GPU::VRAMFlat_BOBJExtPal, 0, 8192);
	unsigned e = *(volatile u8 *)(VitaNative_GfxRegisters + 0x244);
	if ((e & 0x87) == 0x84)
		memcpy(GPU::VRAMFlat_ABGExtPal, s_HW_LCDC_VRAM + 0x80000, 32768);
	for (unsigned i = 0; i < 2; i++) {
		unsigned c = *(volatile u8 *)(VitaNative_GfxRegisters + 0x245 + i);
		const u8 *src = s_HW_LCDC_VRAM + 0x90000 + i * 0x4000;
		if ((c & 0x87) == 0x84)
			memcpy(GPU::VRAMFlat_ABGExtPal + ((c & 8) ? 16384 : 0), src, 16384);
		if ((c & 0x87) == 0x85)
			memcpy(GPU::VRAMFlat_AOBJExtPal, src, 8192);
	}
	if ((*(volatile u8 *)(VitaNative_GfxRegisters + 0x248) & 0x87) == 0x82)
		memcpy(GPU::VRAMFlat_BBGExtPal, s_HW_LCDC_VRAM + 0x98000, 32768);
	if ((*(volatile u8 *)(VitaNative_GfxRegisters + 0x249) & 0x87) == 0x83)
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

extern "C" unsigned VitaNativeRenderLastDrawMask() { return lastDraw; }
extern "C" unsigned VitaNativeRenderFrameCount() { return frames; }

extern "C" int VitaNativeRenderInit()
{
	if (initialized)
		return 0;
	frames = 0;
	topCache.valid = false;
	bottomCache.valid = false;
	previousPower = ~0u;
	grace = 0;
	engineA.Reset();
	engineB.Reset();

	if (VitaGpuInit() < 0)
		return -1;

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

static int Present()
{
	if (!openFrame)
		return -1;
	openFrame = false;

	unsigned power = reg16(0x304), a = reg32(0), b = reg32(0x1000);
	unsigned long long phase = VitaOS_Now();
	(void)b;

	/* Finish the 3D and read it back, but only when engine A is actually showing it: the display
	 * has to be on, in its normal 2D mode, with BG0 enabled and taking its content from the 3D
	 * engine. Outside the field and battle that is not the case, and then there is no reason to
	 * make the CPU wait for the GPU at all. */
	VitaNativeG3FrameEnd((power & 2) && ((a >> 16) & 3) == 1 && (a & (1 << 8)) && (a & (1 << 3)));

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

	mapMemory();
	bool drawA = mappingChanged || !TopSame(power, a);
	if (drawB && !mappingChanged && (power & 0x8000) && BottomSame(power))
		drawB = false;
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
	if (drawA)
		SaveTop(power, a);
	if (drawB)
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

	/* Uploading the panels and drawing them are timed apart: one is a copy into GPU memory that
	 * stalls if the GPU is still reading, the other is two quads and a swap, and a log that adds
	 * them together cannot say which is costing the frame. */
	phase = VitaOS_Now();
	for (unsigned e = 0; e < 2; e++)
		if (lastDraw & (1u << e))
			VitaGpuPanelUpload((int)e, raw[e]);
	uploadUs = (unsigned)(VitaOS_Now() - phase);

	phase = VitaOS_Now();
	/* POWCNT1 bit 15 says which engine is on the physical top screen. */
	VitaGpuPresent((power & 0x8000) != 0);
	presentUs = (unsigned)(VitaOS_Now() - phase);
	frames++;
	return 0;
}

extern "C" int VitaNativeRenderPresent() { return Present(); }
extern "C" int VitaNativeRenderPresentNoWait() { return Present(); }

extern "C" void VitaNativeRenderShutdown()
{
	if (!initialized)
		return;
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

/* 0 = binding the DS registers and memory, 1 = compositing, 2 = putting it on the screen,
 * 3 = uploading the composed panels to the GPU. */
extern "C" unsigned RenderStage(unsigned stage)
{
	return stage == 0 ? bindUs : stage == 1 ? drawUs : stage == 3 ? uploadUs : presentUs;
}

/* What the port has asked the GPU to hold, for the memory report. vitaGL owns its own pools; this is
 * what the renderer has added to them. */
extern "C" void VitaNativeRenderMemStats(unsigned *bytes, unsigned *high, unsigned *blocks)
{
	VitaGpuMemStats(bytes, high, blocks);
}
