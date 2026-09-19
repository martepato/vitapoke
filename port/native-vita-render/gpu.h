/* vitapoke: everything the renderer asks the Vita's GPU to do.
 *
 * This is a deliberate boundary, and a narrow one. The rest of the renderer is DS code -- libntr's
 * headers, melonDS's compositor, the DS's registers -- and that world cannot be compiled in the same
 * translation unit as a GL header: libntr bundles glad, which defines every GL name as a macro for a
 * function pointer, and vitaGL defines the GL types as macros of its own. Each rewrites the other's
 * declarations into nonsense.
 *
 * So the GPU lives behind these eight calls, in plain C types, and gpu.cpp is the only file in the
 * port that includes a GL header. That also means the graphics API is replaceable: writing GXM
 * directly, if the renderer ever needs what vitaGL does not expose, is a rewrite of gpu.cpp and
 * nothing else.
 */
#ifndef VITAPOKE_RENDER_GPU_H
#define VITAPOKE_RENDER_GPU_H

#ifdef __cplusplus
extern "C" {
#endif

/* One vertex of the DS's 3D output, as libntr's geometry simulator produces it: screen coordinates,
 * texels, and a colour with alpha. See g3_backend.cpp for where the numbers come from. */
struct VitaGpuVertex {
	float u, v;
	float r, g, b, a;
	float x, y, z;
};

/* Bring the GPU up, with the two DS panel textures. Returns 0, or negative if the GPU is
 * unavailable -- in which case nothing else here may be called. */
int VitaGpuInit(void);
void VitaGpuShutdown(void);

/* Hand over one freshly composed DS screen: 256x192 pixels, R,G,B,A bytes, with a stride of 256
 * pixels. `index` is the DS engine, 0 or 1. */
void VitaGpuPanelUpload(int index, const void *rgba);

/* Clear the screen, draw the two panels where port/vita/include/vitapoke.h says, and show the
 * result. `topIsEngineA` is the DS's POWCNT1 display-swap bit: which engine is the top screen.
 * Does not wait for vertical blank -- the game does its own pacing. */
void VitaGpuPresent(int topIsEngineA);

/* The DS's 3D output, which the port rasterises into its own 256x192 target.
 *
 * Begin clears it and sets the state the DS's depth test implies. End finishes the drawing and, when
 * `rgbaOut` is not NULL, reads the target back into it (256x192, R,G,B,A bytes, rows top down) for
 * the 2D compositor to blend as a background layer. Passing NULL skips the read, and with it the
 * wait for the GPU, which is worth having on the many frames where nothing shows the 3D layer.
 */
void VitaGpuFrameBegin3D(void);
void VitaGpuFrameEnd3D(void *rgbaOut);

/* A decoded DS texture. `rgba` is w*h pixels of R,G,B,A bytes and is not kept. Returns 0 on
 * failure. */
unsigned VitaGpuTextureCreate(unsigned w, unsigned h, const void *rgba);
void VitaGpuTextureDestroy(unsigned texture, unsigned textureSize);

/* The DS's per-polygon state, which its geometry engine sets between draws. Culling is which face
 * is discarded; the depth comparison is the DS's own, and "less" versus "less or equal" decides
 * whether a coplanar polygon drawn later shows through -- which the DS uses deliberately. */
#define VITAGPU_CULL_NONE    0
#define VITAGPU_CULL_FRONT   1
#define VITAGPU_CULL_BACK    2
#define VITAGPU_DEPTH_LEQUAL 0
#define VITAGPU_DEPTH_LESS   1
/* `depthWrite` is the DS's rule for whether a pixel updates the depth buffer: an opaque polygon
 * always does, a translucent one only when POLYGON_ATTR says so. */
void VitaGpuSetPolygonState(int cull, int depthCompare, int depthWrite);

/* Draw a triangle list into the 3D target. `texture` 0 means untextured; repeat is per axis, the
 * DS's texture wrap. */
void VitaGpuDrawTriangles(unsigned texture, int repeatS, int repeatT,
                          const struct VitaGpuVertex *vertices, unsigned count);

/* What the renderer is holding on the GPU, for the memory report. */
void VitaGpuMemStats(unsigned *bytes, unsigned *high, unsigned *blocks);

#ifdef __cplusplus
}
#endif
#endif /* VITAPOKE_RENDER_GPU_H */
