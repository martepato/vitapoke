/* vitapoke: the DS's 3D output, rasterised by the Vita's GPU.
 *
 * The DS's geometry engine is a fixed-function pipeline: the game pushes matrices, lights, vertices
 * and polygon attributes at it, and it produces one 256x192 image which the 2D engine then treats as
 * a background layer. libntr's simulator already decodes that command stream and does the transform,
 * lighting and clipping on the CPU (g3_handler.cpp and frontend.cpp, both compiled unchanged into
 * this renderer); what arrives here is the result -- triangles in screen coordinates, with texture
 * coordinates in texels, a colour per vertex and a depth.
 *
 * So this file is two things and nothing else: the rasteriser's vertex list, and the cache of
 * decoded DS textures. The GPU itself is behind gpu.h.
 *
 * The finished image is read back into the buffer the 2D compositor blends, which is the same
 * division of labour the PSP build had, where the GE drew into a buffer at a fixed address that the
 * CPU could simply read. Here the read makes the CPU wait for the GPU once a frame, on the frames
 * that use it. Keeping it is what makes the DS's layer ordering, windows and blending come out
 * exactly right, which is the whole reason the 2D side is done in software; compositing the 3D layer
 * on the GPU instead is a much larger change and belongs with the rest of that work.
 *
 * None of this has run against real game frames yet -- the port reached its first link with this
 * file -- so the geometry is unverified. The derivations are written down rather than guessed at
 * again. See docs/VITA.md.
 */
/* ------------------------------------------------------------------ include order matters here
 *
 * libntr's nitro/types.h and melonDS's types.h both define u8..s64, and disagree about what u32 is
 * on this toolchain; nitro's win. render-vita.cpp has the long version of the note. There is no GL
 * header here, and there cannot be: see gpu.h. What this file does get from libntr's bundled glad
 * header is the GL enum names, because the geometry simulator announces the DS's per-polygon state
 * through glad's function pointers and those are defined below.
 */
#include <nitro.h>

#define TYPES_H   /* melonDS's types.h: nitro has already defined these */

#include "../native-render-opt/native_gpu.h"
#include "gpu.h"

#include <simulator/g3_handler.h>
#include <simulator/g3_draw.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void VitaNativeMemLog(const char *, ...);
extern "C" void VitaNativeFatal(const char *);
extern "C" unsigned long long VitaOS_Now(void);
extern "C" u8 s_HW_LCDC_VRAM[0xA4000];

/* Microseconds spent submitting the DS's 3D geometry, accumulated. The game profiler (gameprof.c,
 * DEV builds) subtracts it from the game code it happens inside, so that a scene's cost is reported
 * as the game's own CPU rather than the renderer's. */
extern "C" unsigned g3ProfUs;
unsigned g3ProfUs;

/* ---------------------------------------------------------------- what the simulator expects */

static int cullEnabled, cullFront, depthLess;

extern "C" {

/* The DS's depth buffer holds W, not Z, when the polygon attributes ask for it. The simulator
 * consults this; this port has no W buffer, so it says so rather than pretending. */
u8 s_SIM_useWBuffer = 0;

/* 15-bit BGR to 8 bits a channel, with the low bits replicated so 31 becomes 255. */
void SIM_u16ToRGB(u16 c, u8 *r, u8 *g, u8 *b)
{
	unsigned x = c & 31, y = (c >> 5) & 31, z = (c >> 10) & 31;
	*r = (u8)((x << 3) | (x >> 2));
	*g = (u8)((y << 3) | (y >> 2));
	*b = (u8)((z << 3) | (z >> 2));
}

/* The simulator reaches for a handful of OpenGL entry points through glad's function pointers. They
 * carry the DS's per-polygon state: which face is culled, and how deep is compared. Nothing is
 * called on the GPU here -- the state is recorded and handed to the draw, which is also when the
 * simulator's own ordering makes it true. */
static void APIENTRY NoDebug(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *) { }
static void APIENTRY OnEnable(GLenum c) { if (c == GL_CULL_FACE) cullEnabled = 1; }
static void APIENTRY OnDisable(GLenum c) { if (c == GL_CULL_FACE) cullEnabled = 0; }
static void APIENTRY OnCullFace(GLenum c) { cullFront = (c == GL_FRONT); }
static void APIENTRY OnDepthFunc(GLenum c) { depthLess = (c == GL_LESS); }

PFNGLDEBUGMESSAGEINSERTPROC glad_glDebugMessageInsert = NoDebug;
PFNGLENABLEPROC glad_glEnable = OnEnable;
PFNGLDISABLEPROC glad_glDisable = OnDisable;
PFNGLCULLFACEPROC glad_glCullFace = OnCullFace;
PFNGLDEPTHFUNCPROC glad_glDepthFunc = OnDepthFunc;

}

/* ---------------------------------------------------------------- the texture cache
 *
 * A DS texture is palette indices in VRAM plus a palette somewhere else, in one of six formats. The
 * simulator can decode any of them into 32-bit pixels; doing that every draw would be wasteful, so
 * decoded textures are kept as GPU textures, keyed by everything that decides their contents --
 * including a copy of the source bytes, because the game overwrites texture VRAM in place and
 * nothing else would tell us that it had.
 */
/* How many textures to keep, and how much memory they may take between them.
 *
 * Sixteen slots was inherited from the PSP build and is not enough: on the real game the cache
 * filled during the opening cutscene at 76 KB -- nowhere near its byte budget -- and, with nothing
 * evicted, every texture the title screen then asked for was refused. It drew untextured for eighty
 * thousand frames. The slot count was the binding constraint, so it is now large enough that the
 * budget is what decides, and the budget is what a DS can address: 512 KB of texture VRAM and 128 KB
 * of palette, which cannot expand to more than about 8 MB of 32-bit pixels however it is sliced.
 * Keeping every texture a scene can possibly have is therefore affordable, and it makes the cache
 * behave the same way in a busy scene as in a quiet one. */
#define TEXTURE_SLOTS 256
#define TEXTURE_BUDGET (8 * 1024 * 1024)

struct TextureEntry {
	unsigned offset, pal, format, w, h, color0;
	unsigned texture;
	u8 *snapshot;       /* what it was decoded from: w*h of VRAM, then 64 bytes of palette */
	unsigned bytes;
	unsigned lastUse;   /* the value of `binds` when this entry was last wanted */
};

static TextureEntry cache[TEXTURE_SLOTS];
static unsigned cacheSize, cacheBytes;
static unsigned decodes, binds, hits, evictions;
/* How many times VitaGpuTextureCreate has said no; only the first few reach the log. */
static unsigned refusals;

/* Give up the least recently used entry, so that a scene needing more than the cache holds keeps
 * drawing with textures rather than without. Least recently used, rather than the oldest: a scene
 * change replaces the whole working set, and the entries the new scene is not asking for are exactly
 * the ones that stopped being bound. */
static void EvictOldest(void)
{
	unsigned oldest = 0;

	if (!cacheSize)
		return;
	for (unsigned i = 1; i < cacheSize; i++)
		if (cache[i].lastUse < cache[oldest].lastUse)
			oldest = i;

	VitaGpuTextureDestroy(cache[oldest].texture);
	free(cache[oldest].snapshot);
	cacheBytes -= cache[oldest].bytes;
	/* Fill the hole with the last entry: the cache is searched linearly and has no order of its
	 * own, so there is nothing to preserve by shifting. */
	cache[oldest] = cache[cacheSize - 1];
	cache[cacheSize - 1].snapshot = NULL;
	cache[cacheSize - 1].texture = 0;
	cacheSize--;
	evictions++;
}

/* Returns the GPU texture for the geometry engine's current texture parameters, decoding it first if
 * this is the first time it has been seen. 0 means "draw untextured", which is also what the DS does
 * when the format is none. */
static unsigned CurrentTexture(void)
{
	const G3SIM_TexImageParam_t &p = s_texImageParam;
	TextureEntry *entry = NULL;

	binds++;
	if (p.textureFormat == 0)
		return 0;
	/* Bounds first: these parameters come from game data, and a bad one would be a read off the
	 * end of VRAM. */
	if (p.textureSSize > 512 || p.textureTSize > 512 || p.textureOffset > 0x20000 ||
	    (unsigned)(p.textureSSize * p.textureTSize) > 0x20000 - p.textureOffset ||
	    s_texPlttBase > 0x10000 - 64) {
		VitaNativeFatal("3D texture parameters out of range");
		return 0;
	}

	for (unsigned i = 0; i < cacheSize; i++) {
		TextureEntry &e = cache[i];
		if (e.offset == p.textureOffset && e.pal == s_texPlttBase && e.format == p.textureFormat &&
		    e.w == p.textureSSize && e.h == p.textureTSize && e.color0 == p.color0 &&
		    !memcmp(e.snapshot, s_HW_LCDC_VRAM + p.textureOffset, e.w * e.h) &&
		    !memcmp(e.snapshot + e.w * e.h, s_HW_LCDC_VRAM + 0x80000 + s_texPlttBase, 64)) {
			hits++;
			e.lastUse = binds;
			return e.texture;
		}
	}

	{
		unsigned w = p.textureSSize, h = p.textureTSize, bytes = w * h * 4;
		u8 *pixels;
		u8 *src = s_HW_LCDC_VRAM + p.textureOffset;
		u16 *pal = (u16 *)(s_HW_LCDC_VRAM + 0x80000 + s_texPlttBase);

		/* Make room. One texture cannot be larger than the budget -- the DS cannot address one
		 * that big -- so this terminates with room for it. */
		while ((cacheSize >= TEXTURE_SLOTS || cacheBytes + bytes > TEXTURE_BUDGET) && cacheSize)
			EvictOldest();
		if (cacheSize >= TEXTURE_SLOTS || cacheBytes + bytes > TEXTURE_BUDGET) {
			/* Only reachable if a single texture exceeds the whole budget, which the paragraph
			 * above says cannot happen -- so say so once rather than every frame. */
			if (refusals++ == 0)
				VitaNativeMemLog("[TEXTURE] a %ux%u texture does not fit the cache at all", w, h);
			return 0;
		}
		pixels = (u8 *)malloc(bytes);
		if (!pixels)
			VitaNativeFatal("out of memory decoding a 3D texture");
		switch (p.textureFormat) {
		case GX_TEXFMT_PLTT4:   G3SIM_DecodeTex4(src, pal, pixels, w, h); break;
		case GX_TEXFMT_PLTT16:  G3SIM_DecodeTex16(src, pal, pixels, w, h); break;
		case GX_TEXFMT_PLTT256: G3SIM_DecodeTex256(src, pal, pixels, w, h); break;
		case GX_TEXFMT_A3I5:    G3SIM_DecodeTexA3I5(src, pal, pixels, w, h); break;
		case GX_TEXFMT_A5I3:    G3SIM_DecodeTexA5I3(src, pal, pixels, w, h); break;
		case GX_TEXFMT_DIRECT:  G3SIM_DecodeTexDirect(src, pixels, w, h); break;
		default:
			VitaNativeMemLog("[TEXTURE] unsupported DS texture format %u", p.textureFormat);
			free(pixels);
			return 0;
		}
		entry = &cache[cacheSize];
		entry->snapshot = (u8 *)malloc(w * h + 64);
		if (!entry->snapshot)
			VitaNativeFatal("out of memory snapshotting a 3D texture");
		/* The decoders write R,G,B,A bytes, which is what the GPU layer takes. */
		entry->texture = VitaGpuTextureCreate(w, h, pixels);
		free(pixels);
		if (!entry->texture) {
			free(entry->snapshot);
			entry->snapshot = NULL;
			/* One refusal is worth reading and five thousand are not: a scene that the GPU will
			 * not give textures to asks for the same ones on every frame, and the log is a file
			 * on a memory card. The count that matters is in the [PERF] line. */
			if (++refusals <= 8)
				VitaNativeMemLog("[TEXTURE] the GPU refused a %ux%u texture%s", w, h,
				                 refusals == 8 ? " (not reporting any more of these)" : "");
			return 0;
		}
		memcpy(entry->snapshot, src, w * h);
		memcpy(entry->snapshot + w * h, s_HW_LCDC_VRAM + 0x80000 + s_texPlttBase, 64);
		entry->offset = p.textureOffset;
		entry->pal = s_texPlttBase;
		entry->format = p.textureFormat;
		entry->w = w;
		entry->h = h;
		entry->color0 = p.color0;
		entry->bytes = bytes;
		entry->lastUse = binds;
		cacheSize++;
		cacheBytes += bytes;
		decodes++;
		return entry->texture;
	}
}

/* ---------------------------------------------------------------- the vertex list */

#define VERTEX_LIMIT 6144
static struct VitaGpuVertex vertices[VERTEX_LIMIT];
static unsigned count;
static unsigned polygonsThisFrame;
/* Set once the [G3] line below has been written. */
static unsigned firstPolygonsReported;
static int frameOpen;

/* One transformed, lit and clipped vertex from the simulator.
 *
 * x and y arrive in the DS's normalised device coordinates, already divided through by W, and the
 * texture coordinates as a fraction of the texture. So:
 *
 *   x: -1..1 becomes 0..256 columns        128 + x*128
 *   y: -1..1 becomes 192..0 rows           96 - y*96      (DS rows count down the screen)
 *   z: -1..1, with the far plane at +1
 *   s,t: 0..1 becomes texels
 *
 * z is handed over negated, because glOrtho with a near of -1 and a far of +1 maps an eye z of -1 to
 * the far plane: negating puts the DS's far at the GPU's far, and the depth comparison then means
 * what the DS's means. gpu.cpp sets the projection to match.
 */
extern "C" void G3SIM_AddVtx(G3SIM_Vertex_t *v)
{
	struct VitaGpuVertex *out;

	if (count == VERTEX_LIMIT) {
		VitaNativeMemLog("[RENDER] 3D vertex list full at %u vertices: frame incomplete", count);
		return;
	}
	out = &vertices[count++];
	out->u = v->s * s_texImageParam.textureSSize;
	out->v = v->t * s_texImageParam.textureTSize;
	out->r = v->r;
	out->g = v->g;
	out->b = v->b;
	out->a = v->a;
	out->x = 128.0f + v->x * 128.0f;
	out->y = 96.0f - v->y * 96.0f;
	out->z = -v->z;
}

extern "C" void G3SIM_DrawArray()
{
	unsigned long long started;

	if (!count || !frameOpen)
		return;
	started = VitaOS_Now();
	VitaGpuSetPolygonState(!cullEnabled ? VITAGPU_CULL_NONE
	                                    : cullFront ? VITAGPU_CULL_FRONT : VITAGPU_CULL_BACK,
	                       depthLess ? VITAGPU_DEPTH_LESS : VITAGPU_DEPTH_LEQUAL);
	VitaGpuDrawTriangles(CurrentTexture(), s_texImageParam.repeatS, s_texImageParam.repeatT,
	                     vertices, count);
	polygonsThisFrame += count / 3;
	g3ProfUs += (unsigned)(VitaOS_Now() - started);
}

extern "C" void G3SIM_DrawCleanUp() { count = 0; }

extern "C" void G3SIM_FlushArray()
{
	if (count)
		G3SIM_DrawArray();
	count = 0;
}

/* ---------------------------------------------------------------- the frame */

/* The 2D compositor reads the 3D layer from here, in melonDS's packed form: six bits a channel with
 * the DS's five-bit alpha above them. The read from the GPU is plain 8-bit RGBA, so it is converted
 * on the way in. */
static u32 readback[256 * 192];
static u32 alphaTo5[256];
static int tablesReady;

extern "C" void VitaNativeG3FrameBegin(void)
{
	if (!tablesReady) {
		for (unsigned i = 0; i < 256; i++)
			alphaTo5[i] = ((i * 31 + 127) / 255) << 24;
		tablesReady = 1;
	}
	polygonsThisFrame = 0;
	count = 0;
	frameOpen = 1;
	VitaGpuFrameBegin3D();
}

/* `wanted` is false when the 2D engine is not showing the 3D layer this frame, which is most of the
 * time outside the field and battle. Then nothing is read back and the GPU is not waited for. */
extern "C" void VitaNativeG3FrameEnd(int wanted)
{
	if (!frameOpen)
		return;
	G3SIM_FlushArray();
	frameOpen = 0;
	/* The one line that says the 3D path has work. Most of the opening is 2D, so the first frame with
	 * geometry in it can be minutes into a run, and until it appears nothing in the log distinguishes
	 * "the geometry engine is idle" from "the geometry engine is not wired up". */
	if (polygonsThisFrame && !firstPolygonsReported) {
		firstPolygonsReported = 1;
		VitaNativeMemLog("[G3] first 3D frame: %u polygons, layer %s", polygonsThisFrame,
		                 wanted ? "shown" : "not shown");
	}
	VitaGpuFrameEnd3D(wanted ? readback : NULL);
	if (!wanted)
		return;
	for (unsigned i = 0; i < 256 * 192; i++) {
		u32 c = readback[i];
		GPU3D::NativeFrame[i] = ((c >> 2) & 0x003f3f3fu) | alphaTo5[c >> 24];
	}
}

extern "C" unsigned VitaNativeG3Polygons(void) { return polygonsThisFrame; }

extern "C" void VitaNativeG3TextureStats(unsigned *entries, unsigned *bytes, unsigned *bindCount,
                                         unsigned *hitCount, unsigned *decodeCount,
                                         unsigned *evictionCount)
{
	if (entries)
		*entries = cacheSize;
	if (bytes)
		*bytes = cacheBytes;
	if (bindCount)
		*bindCount = binds;
	if (hitCount)
		*hitCount = hits;
	if (decodeCount)
		*decodeCount = decodes;
	if (evictionCount)
		*evictionCount = evictions;
}

extern "C" void VitaNativeG3Release(void)
{
	for (unsigned i = 0; i < cacheSize; i++) {
		VitaGpuTextureDestroy(cache[i].texture);
		free(cache[i].snapshot);
	}
	cacheSize = 0;
	cacheBytes = 0;
	count = 0;
	frameOpen = 0;
}
