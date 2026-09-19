/* vitapoke: the NEON 2D kernels against the scalar definitions they replace.
 *
 * port/native-render-opt/neon2d.h carries each kernel twice: the scalar version is what
 * GPU2D_Soft.cpp did, and the NEON version is what the console runs. This runs both over
 * pseudorandom input and fails on the first pixel where they differ, so that "the same arithmetic,
 * four at a time" is something the build checks rather than something the comment claims.
 *
 * Pseudorandom, not exhaustive: a 32-bit pixel has four billion values and there are five kernels.
 * What the input is chosen to cover instead is the shape of the data -- the flag byte that drives
 * the effect decision takes its real values rather than uniform noise, because uniform noise would
 * almost never produce the sprite and 3D-layer combinations that decide the interesting branch.
 *
 * It is built and run by tests/vita/run.sh on the host, because none of these kernels touches the
 * console: NEON is the only thing that makes them Vita code, and a host with NEON runs them the
 * same. Where the host has none, the header's scalar fallback is what gets compared against itself,
 * which still checks the plumbing and says so.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "neon2d.h"

#define PIXELS 256

static uint32_t seed = 0x13579BDFu;

static uint32_t Next(void)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

/* A composed pixel: six bits a channel, and a flag byte that says which layer put it there. The
 * flag values are the ones the compositor actually produces -- a background layer's 0x01 to 0x08,
 * the 3D layer's 0x40, a sprite's 0x80, a semi-transparent sprite's 0xC0 with its coefficient, and
 * the backdrop's 0x20. */
static uint32_t Pixel(void)
{
	static const uint32_t flags[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xC0 };
	uint32_t r = Next();
	uint32_t flag = flags[(r >> 28) % (sizeof(flags) / sizeof(flags[0]))];

	if (flag == 0xC0)
		flag |= (r >> 3) & 0x1F;
	return (r & 0x003F3F3Fu) | (flag << 24);
}

static int failures;

static void Differ(const char *what, unsigned i, uint32_t got, uint32_t want)
{
	if (failures++ < 8)
		printf("    %s: pixel %u is %08x, should be %08x\n", what, i, (unsigned)got,
		       (unsigned)want);
}

static void Compare(const char *what, const uint32_t *got, const uint32_t *want, unsigned count)
{
	for (unsigned i = 0; i < count; i++)
		if (got[i] != want[i])
			Differ(what, i, got[i], want[i]);
}

int main(void)
{
	static uint32_t src[PIXELS], a[PIXELS], b[PIXELS];
	static uint32_t neonLine[PIXELS * 3], scalarLine[PIXELS * 3];
	static uint32_t line3d[PIXELS];
	static uint8_t window[PIXELS];
	static uint8_t neonMask[PIXELS], scalarMask[PIXELS];

#ifdef VITAPOKE_NEON
	printf("[NEON2D] built with NEON\n");
#else
	printf("[NEON2D] no NEON on this host: comparing the scalar path against itself\n");
#endif

	for (unsigned round = 0; round < 64; round++) {
		for (unsigned i = 0; i < PIXELS; i++) {
			src[i] = Pixel();
			line3d[i] = (Next() & 0x003F3F3Fu) |
			            ((round & 1) ? 0u : ((Next() & 0x1F) << 24));   /* some transparent */
			window[i] = (uint8_t)Next();
		}

		/* fill */
		{
			uint32_t value = Next();

			Neon2D_Fill(a, value, PIXELS);
			Neon2D_FillScalar(b, value, PIXELS);
			Compare("fill", a, b, PIXELS);
		}

		/* output conversion */
		Neon2D_Output(a, src, PIXELS);
		Neon2D_OutputScalar(b, src, PIXELS);
		Compare("output", a, b, PIXELS);

		/* master brightness, every factor the DS allows */
		for (uint32_t factor = 0; factor <= 16; factor++) {
			memcpy(a, src, sizeof src);
			memcpy(b, src, sizeof src);
			Neon2D_BrightnessUp(a, factor, 0x8, PIXELS);
			Neon2D_BrightnessUpScalar(b, factor, 0x8, PIXELS);
			Compare("brightness up", a, b, PIXELS);

			memcpy(a, src, sizeof src);
			memcpy(b, src, sizeof src);
			Neon2D_BrightnessDown(a, factor, 0x7, PIXELS);
			Neon2D_BrightnessDownScalar(b, factor, 0x7, PIXELS);
			Compare("brightness down", a, b, PIXELS);
		}

		/* the 3D layer: both the line it lands on and the one it pushes down */
		for (unsigned i = 0; i < PIXELS * 3; i++)
			neonLine[i] = scalarLine[i] = Pixel();
		Neon2D_BG3D(neonLine, line3d, window, PIXELS);
		Neon2D_BG3DScalar(scalarLine, line3d, window, PIXELS);
		Compare("3D layer", neonLine, scalarLine, PIXELS);
		Compare("3D layer, pushed down", neonLine + PIXELS, scalarLine + PIXELS, PIXELS);

		/* the 3D layer read back from the GPU, over every alpha the target can hold */
		for (unsigned i = 0; i < PIXELS; i++)
			src[i] = (Next() & 0x00FFFFFFu) | ((i & 0xFF) << 24);
		Neon2D_Readback(a, src, PIXELS);
		Neon2D_ReadbackScalar(b, src, PIXELS);
		Compare("3D readback", a, b, PIXELS);

		for (unsigned i = 0; i < PIXELS; i++)
			src[i] = Pixel();

		/* the effect mask, over the BLDCNT values that select each effect */
		for (unsigned k = 0; k < 8; k++) {
			uint32_t blendCnt = (Next() & 0x3FFFu) | ((k & 3u) << 6);

			memset(neonMask, 0xAA, sizeof neonMask);
			memset(scalarMask, 0x55, sizeof scalarMask);
			Neon2D_EffectMask(neonMask, src, src + 1, window, blendCnt, PIXELS - 1);
			Neon2D_EffectMaskScalar(scalarMask, src, src + 1, window, blendCnt, PIXELS - 1);
			for (unsigned i = 0; i < PIXELS - 1; i++)
				if (!neonMask[i] != !scalarMask[i])
					Differ("effect mask", i, neonMask[i], scalarMask[i]);
		}
	}

	if (failures) {
		printf("[NEON2D] %d mismatches\n", failures);
		return 1;
	}
	printf("[NEON2D] the NEON kernels agree with the scalar ones\n");
	return 0;
}
