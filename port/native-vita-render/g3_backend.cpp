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
#include "neon2d.h"

#include <simulator/g3_handler.h>
#include <simulator/g3_draw.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void VitaNativeMemLog(const char *, ...);
extern "C" void VitaNativeFatal(const char *);
extern "C" unsigned long long VitaOS_Now(void);
extern "C" u8 s_HW_LCDC_VRAM[0xA4000];
extern "C" unsigned char VitaNative_GfxRegisters[];

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
/* Swapped back, deliberately.
 *
 * The simulator does not pass the DS's choice through: for GX_CULL_BACK -- render the front surface,
 * which is what nearly every model in this game asks for -- it calls glCullFace(GL_FRONT), and for
 * GX_CULL_FRONT it calls GL_BACK. That is not a mistake there. Its own desktop projection reflects
 * the image, a reflection reverses the winding of every triangle, and reversing which face is culled
 * is how it compensates.
 *
 * This port's projection does not reflect anything. G3SIM_AddVtx maps the DS's x of -1..1 to 0..256
 * and its y of -1..1 to 192..0, and the glOrtho in gpu.cpp has its bottom at 192 and its top at 0;
 * compose the two and the result is the identity, x and y both arriving in clip space exactly as the
 * DS produced them. So a triangle keeps the winding it had, there is nothing to compensate for, and
 * taking the simulator's swap at face value culled precisely the faces that should have been drawn.
 *
 * What that looks like is the whole of the symptom: single-sided geometry -- the ground, the walls,
 * the surface of every model -- disappears, while anything the game marks as double-sided stays. A
 * field that is mostly black with a few textures in it, and a title screen with no Giratina on it. */
static void APIENTRY OnCullFace(GLenum c) { cullFront = (c == GL_BACK); }
static void APIENTRY OnDepthFunc(GLenum c) { depthLess = (c == GL_LESS); }

PFNGLDEBUGMESSAGEINSERTPROC glad_glDebugMessageInsert = NoDebug;
PFNGLENABLEPROC glad_glEnable = OnEnable;
PFNGLDISABLEPROC glad_glDisable = OnDisable;
PFNGLCULLFACEPROC glad_glCullFace = OnCullFace;
PFNGLDEPTHFUNCPROC glad_glDepthFunc = OnDepthFunc;

}

/* ---------------------------------------------------------------- where texture memory really is
 *
 * The geometry engine addresses textures and their palettes in flat spaces of its own -- 512 KB of
 * image in four 128 KB slots, 96 KB of palette in six 16 KB ones -- and which VRAM bank serves which
 * slot is whatever the game last wrote to VRAMCNT. The DS SDK offers a fixed menu of arrangements
 * and Pokemon Platinum uses most of it: the opening and the title screen put textures in bank A with
 * their palettes in E, and the field, the battles and the menus put the palettes in F and G instead.
 *
 * Reading the palette from bank E regardless is why the overworld drew black. In the field E is
 * engine A's sprite graphics, so what this read as sixteen colours was whatever happened to be
 * there, and where nothing had been written it was zeroes: every texel colour 0, which with the
 * transparency bit set is every texel invisible. The textures that still appeared were the
 * direct-colour ones, which have no palette to get wrong.
 *
 * So both spaces are resolved here, a slot at a time, from the registers the SDK writes. Nothing is
 * assumed about which bank is where, and a slot no bank serves reads as nothing rather than as
 * somebody else's memory.
 */

static unsigned bankReg(unsigned index)   /* 0 = VRAMCNT_A ... 8 = VRAMCNT_I */
{
	return *(volatile u8 *)(VitaNative_GfxRegisters + 0x240 + index);
}

/* The LCDC offset of one 128 KB texture-image slot, or ~0 when no bank is mapped to it. Banks A and
 * B have a two-bit MST field where C and D have three; in all four, texture is MST 3 and the slot is
 * the OFS field. */
static unsigned texImageSlot(unsigned slot)
{
	static const unsigned lcdc[4] = { 0x00000, 0x20000, 0x40000, 0x60000 };
	static const unsigned mst[4] = { 0x03, 0x03, 0x07, 0x07 };

	for (unsigned i = 0; i < 4; i++) {
		unsigned c = bankReg(i);
		if ((c & 0x80) && (c & mst[i]) == 3 && ((c >> 3) & 3) == slot)
			return lcdc[i];
	}
	return ~0u;
}

/* The LCDC offset of one 16 KB texture-palette slot, or ~0 when no bank is mapped to it. Bank E is
 * 64 KB and covers slots 0 to 3 in one go; F and G are 16 KB each and their OFS selects slot 0, 1, 4
 * or 5. */
static unsigned texPlttSlot(unsigned slot)
{
	unsigned c = bankReg(4);

	if ((c & 0x87) == 0x83 && slot < 4)
		return 0x80000 + slot * 0x4000;
	for (unsigned i = 0; i < 2; i++) {
		c = bankReg(5 + i);
		if ((c & 0x87) == 0x83) {
			unsigned ofs = (c >> 3) & 3;
			if ((ofs & 1) + (ofs >> 1) * 4 == slot)
				return 0x90000 + i * 0x4000;
		}
	}
	return ~0u;
}

/* The bytes behind `bytes` of one of those spaces from `addr`, or NULL if any of it is in a slot no
 * bank serves. A run may cross a slot boundary, which textures larger than a slot do, but only where
 * the next slot is the next block of LCDC along -- which is what consecutive banks give, and is the
 * only way the DS's flat space is contiguous in memory. */
static const u8 *texRange(unsigned addr, unsigned bytes, unsigned slotSize,
                          unsigned (*resolve)(unsigned))
{
	unsigned first = addr / slotSize;
	unsigned last = (addr + (bytes ? bytes - 1 : 0)) / slotSize;
	unsigned base = resolve(first);

	if (base == ~0u)
		return NULL;
	for (unsigned s = first + 1; s <= last; s++)
		if (resolve(s) != base + (s - first) * slotSize)
			return NULL;
	return s_HW_LCDC_VRAM + base + (addr - first * slotSize);
}

/* How much of each space one texture occupies: the DS's six formats at 2, 4, 8 and 16 bits a texel,
 * and the palette each of them indexes. Direct colour has no palette. Zero means the format is not
 * one this port decodes. */
static unsigned texImageBytes(unsigned format, unsigned w, unsigned h)
{
	switch (format) {
	case GX_TEXFMT_PLTT4:   return w * h / 4;
	case GX_TEXFMT_PLTT16:  return w * h / 2;
	case GX_TEXFMT_PLTT256: return w * h;
	case GX_TEXFMT_A3I5:    return w * h;
	case GX_TEXFMT_A5I3:    return w * h;
	case GX_TEXFMT_DIRECT:  return w * h * 2;
	default:                return 0;
	}
}

static unsigned texPlttBytes(unsigned format)
{
	switch (format) {
	case GX_TEXFMT_PLTT4:   return 4 * 2;
	case GX_TEXFMT_PLTT16:  return 16 * 2;
	case GX_TEXFMT_PLTT256: return 256 * 2;
	case GX_TEXFMT_A3I5:    return 32 * 2;
	case GX_TEXFMT_A5I3:    return 8 * 2;
	default:                return 0;
	}
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
	u8 *snapshot;       /* what it was decoded from: the texture's bytes, then its palette's */
	unsigned imageBytes, plttBytes;
	unsigned bytes;
	unsigned lastUse;   /* the value of `binds` when this entry was last wanted */
};

static TextureEntry cache[TEXTURE_SLOTS];
static unsigned cacheSize, cacheBytes;
static unsigned decodes, binds, hits, evictions;
/* How many times VitaGpuTextureCreate has said no, how many textures wanted a format this port does
 * not decode, and how many asked for a VRAM slot no bank was mapped to. Only the first few of each
 * reach the log. */
static unsigned refusals, unsupportedFormats, unmapped;

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

	VitaGpuTextureDestroy(cache[oldest].texture, cache[oldest].bytes);
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

	const unsigned imageBytes = texImageBytes(p.textureFormat, p.textureSSize, p.textureTSize);
	const unsigned plttBytes = texPlttBytes(p.textureFormat);

	if (!imageBytes) {
		/* The one format this port does not decode is 4x4 block compression, which needs a second
		 * run of palette indices in a different place. Say so once; the alternative to drawing it
		 * untextured would be drawing it wrong. */
		if (unsupportedFormats++ == 0)
			VitaNativeMemLog("[TEXTURE] DS texture format %u is not decoded here", p.textureFormat);
		return 0;
	}
	/* Sizes are a three-bit field each, so they cannot exceed 1024; anything else would mean the
	 * simulator handed over something it did not decode, which is a bug in this port rather than in
	 * the game's data. */
	if (p.textureSSize > 1024 || p.textureTSize > 1024)
		VitaNativeFatal("3D texture size out of range");

	/* Where the DS is really keeping this texture and its palette this frame. Either can come back
	 * NULL, which on hardware is a slot with no bank behind it: nothing to read, so nothing to
	 * draw with. */
	const u8 *src = texRange(p.textureOffset, imageBytes, 0x20000, texImageSlot);
	const u16 *pal = plttBytes
	                     ? (const u16 *)texRange(s_texPlttBase, plttBytes, 0x4000, texPlttSlot)
	                     : NULL;

	/* Which banks this scene is really using, each combination once. They are the difference between
	 * "the field draws black" and "the field's palettes are in F and G and this port was reading E".
	 *
	 * Once per *combination*, not once per change: the field legitimately alternates between two
	 * texture banks from one draw to the next, so logging every change wrote hundreds of lines a
	 * frame to the memory card and took the frame rate to single digits. */
	{
		/* The bank an address landed in, rather than the address: the low bits are the texture's own
		 * place inside it and change with every bind. */
		unsigned imageBank = src ? ((unsigned)((const u8 *)src - s_HW_LCDC_VRAM) & ~0x1FFFFu) : ~0u;
		unsigned plttBank = pal ? ((unsigned)((const u8 *)pal - s_HW_LCDC_VRAM) & ~0x3FFFu) : ~0u;
		static unsigned seen[16][2];
		static unsigned seenCount = 0;
		unsigned i;

		for (i = 0; i < seenCount; i++)
			if (seen[i][0] == imageBank && seen[i][1] == plttBank)
				break;
		if (i == seenCount && seenCount < 16) {
			seen[seenCount][0] = imageBank;
			seen[seenCount][1] = plttBank;
			seenCount++;
			VitaNativeMemLog("[TEXTURE] images in LCDC %05x, palettes in LCDC %05x", imageBank,
			                 plttBytes ? plttBank : ~0u);
		}
	}

	if (!src || (plttBytes && !pal)) {
		if (unmapped++ < 8)
			VitaNativeMemLog("[TEXTURE] no VRAM bank holds texture %05x/palette %05x%s",
			                 p.textureOffset, s_texPlttBase,
			                 unmapped == 8 ? " (not reporting any more of these)" : "");
		return 0;
	}

	for (unsigned i = 0; i < cacheSize; i++) {
		TextureEntry &e = cache[i];
		if (e.offset == p.textureOffset && e.pal == s_texPlttBase && e.format == p.textureFormat &&
		    e.w == p.textureSSize && e.h == p.textureTSize && e.color0 == p.color0 &&
		    !memcmp(e.snapshot, src, imageBytes) &&
		    (!plttBytes || !memcmp(e.snapshot + imageBytes, pal, plttBytes))) {
			hits++;
			e.lastUse = binds;
			return e.texture;
		}
	}

	{
		unsigned w = p.textureSSize, h = p.textureTSize, bytes = w * h * 4;
		u8 *pixels;

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
		/* The decoders take the bytes as they lie in VRAM; const is dropped because libntr's
		 * signatures predate it, and none of them writes through these. */
		u8 *image = const_cast<u8 *>(src);
		u16 *colors = const_cast<u16 *>(pal);

		switch (p.textureFormat) {
		case GX_TEXFMT_PLTT4:   G3SIM_DecodeTex4(image, colors, pixels, w, h); break;
		case GX_TEXFMT_PLTT16:  G3SIM_DecodeTex16(image, colors, pixels, w, h); break;
		case GX_TEXFMT_PLTT256: G3SIM_DecodeTex256(image, colors, pixels, w, h); break;
		case GX_TEXFMT_A3I5:    G3SIM_DecodeTexA3I5(image, colors, pixels, w, h); break;
		case GX_TEXFMT_A5I3:    G3SIM_DecodeTexA5I3(image, colors, pixels, w, h); break;
		case GX_TEXFMT_DIRECT:  G3SIM_DecodeTexDirect(image, pixels, w, h); break;
		default:
			free(pixels);
			return 0;
		}
		entry = &cache[cacheSize];
		entry->snapshot = (u8 *)malloc(imageBytes + plttBytes);
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
		memcpy(entry->snapshot, src, imageBytes);
		if (plttBytes)
			memcpy(entry->snapshot + imageBytes, pal, plttBytes);
		entry->imageBytes = imageBytes;
		entry->plttBytes = plttBytes;
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
/* A handful of draw calls, once every so often, with everything that decides whether a polygon can
 * be seen: how many vertices, which texture and format, and the first vertex's position, colour and
 * alpha. Two rounds of reading this code have each found a real bug and neither was the one on the
 * screen, so this is here to say what the geometry actually looks like by the time it reaches the
 * GPU rather than what it ought to look like. */
static unsigned g3DiagFrame, g3DiagLeft;
/* Frames the 2D engine wanted the 3D layer on, and opaque pixels found in the read-back
 * (sampled one in sixteen, so a full screen is 3072). */
static unsigned g3Wanted, g3Lit;
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
	/* Straight through, because they are already what the GPU wants.
	 *
	 * The geometry engine's texture coordinates are in texels, and the simulator has already divided
	 * them by the texture's size when it built this vertex -- G3SIM_Vtx does it, and the desktop
	 * simulator hands the result to its shader as it stands, because a sampler takes a fraction of
	 * the texture and not a texel count. Multiplying the size back in here turned every coordinate
	 * into a texel index and then gave it to glTexCoordPointer, which reads a fraction: a quad that
	 * should have shown one copy of a 64-pixel texture asked for sixty-four of them, and got them,
	 * because the DS's textures mostly repeat.
	 *
	 * On screen that is a recognisable shape in roughly the right colours with a fine stripe through
	 * everything, which is what the field and the title screen's Giratina both looked like once they
	 * were drawn at all. Coordinates past 1.0 still mean what they meant; the wrap mode the DS asked
	 * for is set from repeatS and repeatT at the draw. */
	out->u = v->s;
	out->v = v->t;
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
	/* Whether this polygon may write depth. Alpha 31 is opaque and always does; alpha 0 is the
	 * DS's wireframe mode rather than an invisible polygon, and also does. Everything between is
	 * translucent, and only writes depth when POLYGON_ATTR's bit 11 asks for it -- otherwise a
	 * translucent polygon would hide the ones drawn behind it afterwards. */
	const unsigned polyAlpha = s_curPolygonAttr.alphaInt;
	const int depthWrite =
	    (polyAlpha == 31 || polyAlpha == 0) ? 1 : (int)s_curPolygonAttr.translucentDepth;

	VitaGpuSetPolygonState(!cullEnabled ? VITAGPU_CULL_NONE
	                                    : cullFront ? VITAGPU_CULL_FRONT : VITAGPU_CULL_BACK,
	                       depthLess ? VITAGPU_DEPTH_LESS : VITAGPU_DEPTH_LEQUAL, depthWrite);
	unsigned diagTexture = CurrentTexture();

	if (g3DiagLeft) {
		const struct VitaGpuVertex &v = vertices[0];

		g3DiagLeft--;
		VitaNativeMemLog("[G3DIAG] verts=%u tex=%u fmt=%u c0=%u zw=%d attr=%02x light=%x cull=%d "
		                 "xy=%d,%d z=%d rgba=%d,%d,%d,%d",
		                 count, diagTexture, (unsigned)s_texImageParam.textureFormat,
		                 (unsigned)s_texImageParam.color0, depthWrite,
		                 (unsigned)s_curPolygonAttr.alphaInt, (unsigned)s_curPolygonAttr.lightFlag,
		                 cullEnabled ? (cullFront ? 1 : 2) : 0,
		                 (int)v.x, (int)v.y, (int)(v.z * 1000.0f),
		                 (int)(v.r * 255.0f), (int)(v.g * 255.0f), (int)(v.b * 255.0f),
		                 (int)(v.a * 255.0f));
	}
	VitaGpuDrawTriangles(diagTexture, s_texImageParam.repeatS, s_texImageParam.repeatT,
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

extern "C" void VitaNativeG3FrameBegin(void)
{
	if (++g3DiagFrame % 600 == 0)
		g3DiagLeft = 6;
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
	/* Whether the 2D engine asked for the 3D layer at all, and how much the GPU actually put in it.
	 * Between the vertex list and the screen there are two places the 3D can vanish without a trace:
	 * the 2D engine not showing the layer, and the GPU drawing nothing into it. One counter each
	 * says which, instead of a third round of reading the code. */
	g3Wanted += wanted ? 1 : 0;
	VitaGpuFrameEnd3D(wanted ? readback : NULL);
	if (!wanted)
		return;
	{
		unsigned lit = 0;

		for (unsigned i = 0; i < 256 * 192; i += 16)
			if (readback[i] >> 24)
				lit++;
		g3Lit += lit;
	}
	/* 49,152 pixels, on the game thread, on every frame the field and battles draw: worth doing four
	 * at a time. The kernel and the table below compute the same thing; see neon2d.h. */
	Neon2D_Readback(reinterpret_cast<uint32_t *>(GPU3D::NativeFrame),
	                reinterpret_cast<const uint32_t *>(readback), 256 * 192);
}

extern "C" unsigned VitaNativeG3Polygons(void) { return polygonsThisFrame; }

/* Set by G3SIM_SubmitPolygon in the simulator front end; see the note there. */
extern "C" unsigned g3DroppedW, g3DroppedFar;
unsigned g3DroppedW, g3DroppedFar;

extern "C" void VitaNativeG3LayerTake(unsigned *wantedFrames, unsigned *litPixels)
{
	if (wantedFrames)
		*wantedFrames = g3Wanted;
	if (litPixels)
		*litPixels = g3Lit;
	g3Wanted = g3Lit = 0;
}

extern "C" void VitaNativeG3DroppedTake(unsigned *atW, unsigned *offScreen)
{
	if (atW)
		*atW = g3DroppedW;
	if (offScreen)
		*offScreen = g3DroppedFar;
	g3DroppedW = g3DroppedFar = 0;
}

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
		VitaGpuTextureDestroy(cache[i].texture, cache[i].bytes);
		free(cache[i].snapshot);
	}
	cacheSize = 0;
	cacheBytes = 0;
	count = 0;
	frameOpen = 0;
}
