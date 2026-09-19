/* vitapoke: the parts of the DS 2D compositor that are the same arithmetic on every pixel, done
 * four pixels at a time.
 *
 * Composing one DS screen in software costs about 8 ms on the console, which at two vertical blanks
 * a frame is a quarter of the budget and the largest thing left after the game's own update. It is
 * not one hot loop: a scanline goes through the backdrop fill, the window mask, each background
 * layer, the sprites, the colour special effects, the master brightness and a final format
 * conversion, each of them a pass over 256 pixels.
 *
 * Only some of those passes are the same arithmetic on every pixel, and those are the ones here. The
 * background tile layers are not: each pixel is a lookup into a palette chosen by its tile, and
 * NEON on this CPU has no gather, so eight scattered 16-bit reads stay eight scattered 16-bit reads.
 * The sprite passes are per sprite and irregular for the same reason. What is left -- the fill, the
 * 3D layer, the brightness, the format conversion, and deciding which pixels the colour effects
 * touch at all -- is uniform, and is what this file does.
 *
 * Every kernel is written twice. The NEON version is what runs on the console; the scalar version
 * above it is the definition of what it must produce, copied from GPU2D_Soft.cpp so the two can be
 * compared. tests/vita/neon2d_check.c runs both over pseudorandom input and fails on the first
 * pixel that differs, so "the same arithmetic" is checked rather than asserted.
 *
 * The arithmetic is exact, not approximate. Where a kernel multiplies two 6-bit fields packed in one
 * word -- the brightness ones do -- the note says why the product cannot carry from one into the
 * other, because that is the property the packing depends on and it is not obvious.
 */
#ifndef VITAPOKE_NEON2D_H
#define VITAPOKE_NEON2D_H

#include <stdint.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define VITAPOKE_NEON 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- the scalar definitions
 *
 * These are the reference. They are what GPU2D_Soft.cpp did before, unchanged in behaviour, and the
 * check compares the NEON kernels against them.
 */

static inline void Neon2D_FillScalar(uint32_t *dst, uint32_t value, unsigned count)
{
	for (unsigned i = 0; i < count; i++)
		dst[i] = value;
}

/* The composed line, 6 bits a channel in a 32-bit word, to what the panel texture takes: 8 bits a
 * channel, the low two bits replicated from the high two so that white stays white. */
static inline void Neon2D_OutputScalar(uint32_t *dst, const uint32_t *src, unsigned count)
{
	for (unsigned i = 0; i < count; i++) {
		uint32_t c = (src[i] << 2) & 0x00FCFCFCu;
		dst[i] = c | ((c & 0x00C0C0C0u) >> 6) | 0xFF000000u;
	}
}

static inline void Neon2D_BrightnessUpScalar(uint32_t *dst, uint32_t factor, uint32_t bias,
                                             unsigned count)
{
	for (unsigned i = 0; i < count; i++) {
		uint32_t rb = dst[i] & 0x3F003Fu;
		uint32_t g = dst[i] & 0x003F00u;

		rb += ((((0x3F003Fu - rb) * factor) + (bias * 0x010001u)) >> 4) & 0x3F003Fu;
		g += ((((0x003F00u - g) * factor) + (bias * 0x000100u)) >> 4) & 0x003F00u;
		dst[i] = rb | g | 0xFF000000u;
	}
}

static inline void Neon2D_BrightnessDownScalar(uint32_t *dst, uint32_t factor, uint32_t bias,
                                               unsigned count)
{
	for (unsigned i = 0; i < count; i++) {
		uint32_t rb = dst[i] & 0x3F003Fu;
		uint32_t g = dst[i] & 0x003F00u;

		rb -= (((rb * factor) + (bias * 0x010001u)) >> 4) & 0x3F003Fu;
		g -= (((g * factor) + (bias * 0x000100u)) >> 4) & 0x003F00u;
		dst[i] = rb | g | 0xFF000000u;
	}
}

/* The 3D layer pushed under the backgrounds already drawn: a pixel the 3D engine left transparent,
 * or one the window turns off, keeps what is there. */
static inline void Neon2D_BG3DScalar(uint32_t *bgobj, const uint32_t *line3d, const uint8_t *window,
                                     unsigned count)
{
	for (unsigned i = 0; i < count; i++) {
		uint32_t c = line3d[i];

		if ((c >> 24) == 0)
			continue;
		if (!(window[i] & 0x01))
			continue;
		bgobj[i + 256] = bgobj[i];
		bgobj[i] = c | 0x40000000u;
	}
}

/* ---------------------------------------------------------------- which pixels the effects touch
 *
 * The colour special effects are a decision tree per pixel and then one of five different sums, so
 * vectorising the whole of it would mean computing all five everywhere and throwing four away. What
 * is worth vectorising is the question the tree starts with, because for most pixels of most frames
 * the answer is "none of them" and the composite is then the identity -- the line already holds the
 * value it would be given.
 *
 * So this answers that question for four pixels at a time, and it answers it generously: a pixel it
 * says yes to goes through the exact scalar routine, which decides properly. It only has to never
 * say no when the answer is yes.
 *
 * The tree, from GPU2D_Soft.cpp's ColorComposite:
 *
 *   target2 is 0x1000 for a sprite on the bottom, 0x0100 for the 3D layer, flag2 << 8 otherwise
 *   a sprite on top, or the 3D layer on top, blends if BLDCNT names what is under it
 *   otherwise the effect BLDCNT selects applies if BLDCNT names the top layer and the window allows
 *
 * which makes the generous test: the top pixel is a sprite or the 3D layer and BLDCNT names what is
 * under it, or BLDCNT names the top layer and the window allows an effect and an effect is selected.
 */
static inline unsigned Neon2D_EffectMaybeScalar(uint32_t val1, uint32_t val2, uint8_t window,
                                                uint32_t blendCnt)
{
	uint32_t flag1 = val1 >> 24, flag2 = val2 >> 24;
	uint32_t target2 = (flag2 & 0x80u) ? 0x1000u : ((flag2 & 0x40u) ? 0x0100u : (flag2 << 8));
	uint32_t top = (flag1 & 0x80u) ? 0x10u : ((flag1 & 0x40u) ? 0x01u : flag1);

	if ((flag1 & 0xC0u) && (blendCnt & target2))
		return 1;
	if ((blendCnt & top) && (window & 0x20u) && ((blendCnt >> 6) & 3u))
		return 1;
	return 0;
}

/* Sets one byte per pixel: non-zero where the effects may apply. */
static inline void Neon2D_EffectMaskScalar(uint8_t *out, const uint32_t *val1, const uint32_t *val2,
                                           const uint8_t *window, uint32_t blendCnt, unsigned count)
{
	for (unsigned i = 0; i < count; i++)
		out[i] = (uint8_t)Neon2D_EffectMaybeScalar(val1[i], val2[i], window[i], blendCnt);
}

/* ---------------------------------------------------------------- the 3D layer, read back
 *
 * The GPU renders the DS's 3D into an 8-bit-a-channel target and the 2D compositor wants it the way
 * the DS's own 3D engine would have left it: six bits a channel, and five bits of alpha. A frame is
 * 49,152 pixels and this runs on the game thread, inside the wait the game cannot avoid.
 *
 * The alpha is the only part that is not a shift and a mask. (a * 31 + 127) / 255 rounds eight bits
 * to five, and a division by 255 has an exact integer identity for anything that fits in 16 bits:
 * floor(t / 255) is (t + 1 + ((t + 1) >> 8)) >> 8, and t here is at most 255 * 31 + 127 = 8032.
 */
static inline void Neon2D_ReadbackScalar(uint32_t *dst, const uint32_t *src, unsigned count)
{
	for (unsigned i = 0; i < count; i++) {
		uint32_t c = src[i];
		uint32_t t = (c >> 24) * 31u + 127u;

		dst[i] = ((c >> 2) & 0x003F3F3Fu) | (((t + 1u + ((t + 1u) >> 8)) >> 8) << 24);
	}
}

/* ---------------------------------------------------------------- the NEON versions */

#ifdef VITAPOKE_NEON

static inline void Neon2D_Fill(uint32_t *dst, uint32_t value, unsigned count)
{
	uint32x4_t v = vdupq_n_u32(value);
	unsigned i = 0;

	for (; i + 16 <= count; i += 16) {
		vst1q_u32(dst + i, v);
		vst1q_u32(dst + i + 4, v);
		vst1q_u32(dst + i + 8, v);
		vst1q_u32(dst + i + 12, v);
	}
	for (; i < count; i++)
		dst[i] = value;
}

static inline void Neon2D_Output(uint32_t *dst, const uint32_t *src, unsigned count)
{
	const uint32x4_t keep = vdupq_n_u32(0x00FCFCFCu);
	const uint32x4_t high = vdupq_n_u32(0x00C0C0C0u);
	const uint32x4_t alpha = vdupq_n_u32(0xFF000000u);
	unsigned i = 0;

	for (; i + 4 <= count; i += 4) {
		uint32x4_t c = vandq_u32(vshlq_n_u32(vld1q_u32(src + i), 2), keep);

		vst1q_u32(dst + i, vorrq_u32(vorrq_u32(c, vshrq_n_u32(vandq_u32(c, high), 6)), alpha));
	}
	Neon2D_OutputScalar(dst + i, src + i, count - i);
}

/* Both brightness kernels multiply red and blue together, as one word with red in bits 0 to 5 and
 * blue in bits 16 to 21. That is safe because each field is at most 0x3F and the factor is at most
 * 16, so a product reaches bit 9 at the highest and cannot carry into blue's bits; the mask that
 * follows then keeps only the six bits of each that survive the shift. Green is the same sum in a
 * word of its own. */
static inline void Neon2D_BrightnessUp(uint32_t *dst, uint32_t factor, uint32_t bias,
                                       unsigned count)
{
	const uint32x4_t rbMask = vdupq_n_u32(0x3F003Fu), gMask = vdupq_n_u32(0x003F00u);
	const uint32x4_t rbFull = vdupq_n_u32(0x3F003Fu), gFull = vdupq_n_u32(0x003F00u);
	const uint32x4_t f = vdupq_n_u32(factor);
	const uint32x4_t rbBias = vdupq_n_u32(bias * 0x010001u), gBias = vdupq_n_u32(bias * 0x000100u);
	const uint32x4_t alpha = vdupq_n_u32(0xFF000000u);
	unsigned i = 0;

	for (; i + 4 <= count; i += 4) {
		uint32x4_t v = vld1q_u32(dst + i);
		uint32x4_t rb = vandq_u32(v, rbMask);
		uint32x4_t g = vandq_u32(v, gMask);

		rb = vaddq_u32(rb, vandq_u32(vshrq_n_u32(vaddq_u32(vmulq_u32(vsubq_u32(rbFull, rb), f),
		                                                   rbBias), 4), rbMask));
		g = vaddq_u32(g, vandq_u32(vshrq_n_u32(vaddq_u32(vmulq_u32(vsubq_u32(gFull, g), f),
		                                                 gBias), 4), gMask));
		vst1q_u32(dst + i, vorrq_u32(vorrq_u32(rb, g), alpha));
	}
	Neon2D_BrightnessUpScalar(dst + i, factor, bias, count - i);
}

static inline void Neon2D_BrightnessDown(uint32_t *dst, uint32_t factor, uint32_t bias,
                                         unsigned count)
{
	const uint32x4_t rbMask = vdupq_n_u32(0x3F003Fu), gMask = vdupq_n_u32(0x003F00u);
	const uint32x4_t f = vdupq_n_u32(factor);
	const uint32x4_t rbBias = vdupq_n_u32(bias * 0x010001u), gBias = vdupq_n_u32(bias * 0x000100u);
	const uint32x4_t alpha = vdupq_n_u32(0xFF000000u);
	unsigned i = 0;

	for (; i + 4 <= count; i += 4) {
		uint32x4_t v = vld1q_u32(dst + i);
		uint32x4_t rb = vandq_u32(v, rbMask);
		uint32x4_t g = vandq_u32(v, gMask);

		rb = vsubq_u32(rb, vandq_u32(vshrq_n_u32(vaddq_u32(vmulq_u32(rb, f), rbBias), 4), rbMask));
		g = vsubq_u32(g, vandq_u32(vshrq_n_u32(vaddq_u32(vmulq_u32(g, f), gBias), 4), gMask));
		vst1q_u32(dst + i, vorrq_u32(vorrq_u32(rb, g), alpha));
	}
	Neon2D_BrightnessDownScalar(dst + i, factor, bias, count - i);
}

/* The window mask is a byte a pixel; these widen eight of them to two vectors of four lanes, all
 * bits set where the byte's bit is set, so the result can select with vbslq. */
static inline uint32x4_t Neon2D_WindowBits(const uint8_t *window, unsigned bit, unsigned half)
{
	uint8x8_t w = vld1_u8(window);
	uint16x8_t w16 = vmovl_u8(vand_u8(w, vdup_n_u8((uint8_t)bit)));
	uint32x4_t w32 = half ? vmovl_u16(vget_high_u16(w16)) : vmovl_u16(vget_low_u16(w16));

	return vceqq_u32(w32, vdupq_n_u32(bit));
}

static inline void Neon2D_BG3D(uint32_t *bgobj, const uint32_t *line3d, const uint8_t *window,
                               unsigned count)
{
	const uint32x4_t layer = vdupq_n_u32(0x40000000u);
	const uint32x4_t zero = vdupq_n_u32(0);
	unsigned i = 0;

	for (; i + 8 <= count; i += 8) {
		for (unsigned half = 0; half < 2; half++) {
			unsigned at = i + half * 4;
			uint32x4_t c = vld1q_u32(line3d + at);
			uint32x4_t under = vld1q_u32(bgobj + at);
			/* Opaque, and the window lets the 3D layer through. */
			uint32x4_t take = vandq_u32(vmvnq_u32(vceqq_u32(vshrq_n_u32(c, 24), zero)),
			                            Neon2D_WindowBits(window + i, 0x01, half));

			vst1q_u32(bgobj + at + 256, vbslq_u32(take, under, vld1q_u32(bgobj + at + 256)));
			vst1q_u32(bgobj + at, vbslq_u32(take, vorrq_u32(c, layer), under));
		}
	}
	Neon2D_BG3DScalar(bgobj + i, line3d + i, window + i, count - i);
}

static inline void Neon2D_Readback(uint32_t *dst, const uint32_t *src, unsigned count)
{
	const uint32x4_t rgb = vdupq_n_u32(0x003F3F3Fu);
	const uint32x4_t thirtyOne = vdupq_n_u32(31u);
	const uint32x4_t oneTwentyEight = vdupq_n_u32(128u);   /* the 127 bias, plus the identity's 1 */
	unsigned i = 0;

	for (; i + 4 <= count; i += 4) {
		uint32x4_t c = vld1q_u32(src + i);
		uint32x4_t t = vmlaq_u32(oneTwentyEight, vshrq_n_u32(c, 24), thirtyOne);
		uint32x4_t a = vshrq_n_u32(vaddq_u32(t, vshrq_n_u32(t, 8)), 8);

		vst1q_u32(dst + i, vorrq_u32(vandq_u32(vshrq_n_u32(c, 2), rgb), vshlq_n_u32(a, 24)));
	}
	Neon2D_ReadbackScalar(dst + i, src + i, count - i);
}

static inline void Neon2D_EffectMask(uint8_t *out, const uint32_t *val1, const uint32_t *val2,
                                     const uint8_t *window, uint32_t blendCnt, unsigned count)
{
	const uint32x4_t blend = vdupq_n_u32(blendCnt);
	const uint32x4_t zero = vdupq_n_u32(0);
	const uint32x4_t spriteBit = vdupq_n_u32(0x80u), layerBit = vdupq_n_u32(0x40u);
	const uint32x4_t spriteTarget = vdupq_n_u32(0x1000u), layerTarget = vdupq_n_u32(0x0100u);
	const uint32x4_t spriteTop = vdupq_n_u32(0x10u), layerTop = vdupq_n_u32(0x01u);
	const uint32x4_t effectSelected = vdupq_n_u32(((blendCnt >> 6) & 3u) ? ~0u : 0u);
	unsigned i = 0;

	for (; i + 8 <= count; i += 8) {
		for (unsigned half = 0; half < 2; half++) {
			unsigned at = i + half * 4;
			uint32x4_t flag1 = vshrq_n_u32(vld1q_u32(val1 + at), 24);
			uint32x4_t flag2 = vshrq_n_u32(vld1q_u32(val2 + at), 24);
			uint32x4_t isSprite2 = vmvnq_u32(vceqq_u32(vandq_u32(flag2, spriteBit), zero));
			uint32x4_t isLayer2 = vmvnq_u32(vceqq_u32(vandq_u32(flag2, layerBit), zero));
			uint32x4_t target2 = vbslq_u32(isSprite2, spriteTarget,
			                               vbslq_u32(isLayer2, layerTarget, vshlq_n_u32(flag2, 8)));
			uint32x4_t isSprite1 = vmvnq_u32(vceqq_u32(vandq_u32(flag1, spriteBit), zero));
			uint32x4_t isLayer1 = vmvnq_u32(vceqq_u32(vandq_u32(flag1, layerBit), zero));
			uint32x4_t top = vbslq_u32(isSprite1, spriteTop, vbslq_u32(isLayer1, layerTop, flag1));
			uint32x4_t namesUnder = vmvnq_u32(vceqq_u32(vandq_u32(blend, target2), zero));
			uint32x4_t namesTop = vmvnq_u32(vceqq_u32(vandq_u32(blend, top), zero));
			uint32x4_t forced = vandq_u32(vorrq_u32(isSprite1, isLayer1), namesUnder);
			uint32x4_t ordinary = vandq_u32(vandq_u32(namesTop, Neon2D_WindowBits(window + i, 0x20, half)),
			                                effectSelected);
			uint32x4_t any = vorrq_u32(forced, ordinary);
			uint16x4_t narrow = vmovn_u32(any);

			vst1_lane_u32((uint32_t *)(out + at), vreinterpret_u32_u8(vmovn_u16(vcombine_u16(narrow, narrow))), 0);
		}
	}
	Neon2D_EffectMaskScalar(out + i, val1 + i, val2 + i, window + i, blendCnt, count - i);
}

#else /* no NEON: the scalar definitions are the implementation */

#define Neon2D_Fill            Neon2D_FillScalar
#define Neon2D_Output          Neon2D_OutputScalar
#define Neon2D_BrightnessUp    Neon2D_BrightnessUpScalar
#define Neon2D_BrightnessDown  Neon2D_BrightnessDownScalar
#define Neon2D_BG3D            Neon2D_BG3DScalar
#define Neon2D_EffectMask      Neon2D_EffectMaskScalar
#define Neon2D_Readback        Neon2D_ReadbackScalar

#endif

#ifdef __cplusplus
}
#endif

#endif /* VITAPOKE_NEON2D_H */
