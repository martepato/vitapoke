/* vitapoke: the renderer's GPU layer, on vitaGL.
 *
 * The only file in the port that includes a GL header; see gpu.h for why that is worth arranging.
 *
 * vitaGL rather than GXM directly, and what that does and does not save. GXM will not draw without
 * compiled vertex and fragment programs, and the only compiler available to a homebrew toolchain is
 * Sony's SceShaccCg -- libshacccg.suprx, extracted from a firmware update. vitaGL does not avoid
 * that: it writes the shaders for its fixed-function pipeline as Cg source and compiles them on the
 * console through vitaShaRK, caching each one in ux0:data/shader_cache. So the renderer needs the
 * compiler present either way.
 *
 * What vitaGL saves is writing those shaders, the GXM context, the shader patcher, the memory pools,
 * the render targets and the display queue -- and it is testable, because Vita3K implements
 * SceShaccCg itself. tests/vita/vitagl_features.c pins the features this depends on, so a vitaGL
 * that stopped providing one of them fails the checks rather than the game.
 */
#include <vitaGL.h>
#include <vitashark.h>

#include <stdio.h>
#include <string.h>

#include "gpu.h"

/* Duplicated deliberately rather than including port/vita/include/vitapoke.h: that header is
 * included by DS-side code, and this file must not be in the same include graph as libntr. The
 * panel layout is checked against it in render-vita.cpp, which can see both. */
#define SCREEN_W 960
#define SCREEN_H 544
#define DS_W 256
#define DS_H 192

extern "C" void VitaNativeMemLog(const char *, ...);

static GLuint panel[2];
static GLuint frameBuffer, colorTexture, depthBuffer;
static int ready, targetReady;
static unsigned textureBytes, textureHigh, textureBlocks;
static unsigned readback[DS_W * DS_H];

/* The DS's per-polygon state. Kept here and applied at the next draw rather than set immediately,
 * because the geometry simulator announces it while the vertex list is still being built. */
static int polygonCull = VITAGPU_CULL_NONE;
static int polygonDepth = VITAGPU_DEPTH_LEQUAL;

void VitaGpuSetPolygonState(int cull, int depthCompare)
{
	polygonCull = cull;
	polygonDepth = depthCompare;
}

/* VITAPOKE_NO_GPU: compose, but do not draw.
 *
 * The renderer needs the console's shader compiler (see the note above), and an emulator only has it
 * if the user has put libshacccg.suprx in it. Everything on the DS side of this renderer -- the two 2D
 * engines, the frame, the pacing, the game itself -- does not. This switch lets all of that run and be
 * checked, with FRAME_DUMP writing out what was composed, on a machine that cannot present it.
 *
 * It is for testing and says so in the log. A build for playing does not use it: a player with no
 * shader compiler is better served by the message the renderer prints than by a black screen.
 */
int VitaGpuInit(void)
{
	if (ready)
		return 0;
#ifdef VITAPOKE_NO_GPU
	VitaNativeMemLog("[GPU] built with VITAPOKE_NO_GPU: the DS screens are composed and not drawn");
	return 0;
#endif
	/* vitaGL's pool for immediate-mode geometry. This renderer sends two quads and one triangle
	 * list a frame through it, so it is small on purpose; the textures are allocated separately.
	 *
	 * The return value is not success. Despite the name and the type, vglInit returns GL_TRUE only
	 * when the display size it was asked for did not fit and it fell back to a smaller one -- so a
	 * normal, working init returns GL_FALSE. Treating it as success is a mistake that costs an
	 * afternoon, because the renderer then refuses to start on a perfectly good GPU. What it is
	 * worth doing is saying so when the fallback did happen, since the panel layout assumes
	 * 960x544. */
	if (vglInit(2 * 1024 * 1024) == GL_TRUE)
		VitaNativeMemLog("[GPU] the display is smaller than %dx%d; the panels will be cropped",
		                 SCREEN_W, SCREEN_H);
	/* vitaGL waits for vertical blank inside its own present, and does so by default. The game
	 * already waits for one twice per update, in port/vita/cadence.c, which is its clock: a third
	 * wait here would run the game at 20 frames a second instead of 30, and nothing about the
	 * frame would look wrong while it did. So the present does not wait, and the game keeps its
	 * pacing. */
	vglWaitVblankStart(GL_FALSE);

	/* The shader compiler, brought up here rather than left to vitaGL, for two reasons. It is the
	 * one thing this renderer needs from the console that a console might not have, so a missing
	 * libshacccg.suprx should be a line in the log and a clean exit rather than a crash inside
	 * vitaGL's first draw. And shark_init_simple is the right entry point -- vitaShaRK's own path
	 * for this -- because the extension calls that shark_init makes are not available here (see
	 * port/vita/shacccg_ext.h); initialising it now means vitaGL's own later attempt is a no-op.
	 *
	 * Both paths vitaGL would try are tried, in its order. */
	if (shark_init_simple(NULL) < 0 && shark_init_simple("ur0:data/external/libshacccg.suprx") < 0) {
		VitaNativeMemLog("[GPU] the shader compiler is not installed: vitapoke needs "
		                 "libshacccg.suprx at ur0:data/libshacccg.suprx. Nothing can be drawn "
		                 "without it.");
		return -1;
	}

	glGenTextures(2, panel);
	for (unsigned i = 0; i < 2; i++) {
		glBindTexture(GL_TEXTURE_2D, panel[i]);
		/* Nearest, not linear. A DS pixel doubled is a DS pixel; smoothing would blur text drawn
		 * to be read one pixel per pixel. */
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, DS_W, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	}
	textureBytes = textureHigh = 2u * DS_W * 256u * 4u;
	textureBlocks = 2;
	ready = 1;
	return 0;
}

void VitaGpuShutdown(void)
{
	if (!ready)
		return;
	if (targetReady) {
		glDeleteFramebuffers(1, &frameBuffer);
		glDeleteRenderbuffers(1, &depthBuffer);
		glDeleteTextures(1, &colorTexture);
		targetReady = 0;
	}
	glDeleteTextures(2, panel);
	/* vitaGL has no teardown: it holds its memory and its GXM context for the life of the process,
	 * which on a console is until the application exits and the kernel takes everything back. So
	 * the textures are given up and the rest is left, rather than pretending to release it. */
	ready = 0;
}

void VitaGpuPanelUpload(int index, const void *rgba)
{
	if (!ready || index < 0 || index > 1)
		return;
	glBindTexture(GL_TEXTURE_2D, panel[index]);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DS_W, DS_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	/* Bilinear, because the panel scale is 15/8 and not an integer: with GL_NEAREST the uneven
	 * columns are a visible pattern down the screen. Set here rather than at creation because
	 * vitaGL keeps filter state per texture and this is where the texture is certain to exist. */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

/* One panel: a textured quad in display pixels, showing the used 256x192 of a 256x256 texture.
 *
 * The texture coordinates stop half a texel inside the used area. With bilinear filtering the
 * outermost row and column would otherwise be averaged with whatever lies beyond -- the unused part
 * of the 256x256 texture on two sides, and nothing defined on the others -- which shows up as a
 * one-pixel seam along the edges of both panels. */
static void DrawPanel(GLuint texture, int x, int y, int w, int h)
{
	const float half = 0.5f / 256.0f;
	const float u0 = half, v0 = half;
	const float u1 = (float)DS_W / 256.0f - half;
	const float v1 = (float)DS_H / 256.0f - half;

	glBindTexture(GL_TEXTURE_2D, texture);
	glBegin(GL_TRIANGLE_STRIP);
	glTexCoord2f(u0, v0); glVertex3f((float)x, (float)y, 0.0f);
	glTexCoord2f(u1, v0); glVertex3f((float)(x + w), (float)y, 0.0f);
	glTexCoord2f(u0, v1); glVertex3f((float)x, (float)(y + h), 0.0f);
	glTexCoord2f(u1, v1); glVertex3f((float)(x + w), (float)(y + h), 0.0f);
	glEnd();
}

void VitaGpuPresent(int topIsEngineA)
{
	/* The two DS screens side by side, each filling half the width: 480x360, which is the DS's own
	 * 4:3. See the layout note in port/vita/include/vitapoke.h for why this and not an integer 2x.
	 * The touch screen is on the right, under the player's hand, and port/vita/input.c maps the
	 * front panel into that same rectangle. */
	const int w = SCREEN_W / 2, h = w * DS_H / DS_W;
	const int y = (SCREEN_H - h) / 2;
	unsigned top = topIsEngineA ? 0u : 1u;

	if (!ready)
		return;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, SCREEN_W, SCREEN_H);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, SCREEN_W, SCREEN_H, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_TEXTURE_2D);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	DrawPanel(panel[top], 0, y, w, h);
	DrawPanel(panel[1 - top], SCREEN_W - w, y, w, h);
	glDisable(GL_TEXTURE_2D);
	/* GL_FALSE: the game paces itself on the display's vertical blank in port/vita/cadence.c, and
	 * waiting for another one here would halve the frame rate. */
	vglSwapBuffers(GL_FALSE);
}

/* ---------------------------------------------------------------- the 3D target */

static int CreateTarget(void)
{
	if (targetReady)
		return 1;
	glGenTextures(1, &colorTexture);
	glBindTexture(GL_TEXTURE_2D, colorTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, DS_W, DS_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &frameBuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
	glGenRenderbuffers(1, &depthBuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, DS_W, DS_H);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		VitaNativeMemLog("[GPU] 3D render target incomplete: no 3D will be drawn");
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return 0;
	}
	textureBytes += DS_W * DS_H * 4;
	textureBlocks++;
	if (textureBytes > textureHigh)
		textureHigh = textureBytes;
	targetReady = 1;
	return 1;
}

void VitaGpuFrameBegin3D(void)
{
	if (!ready || !CreateTarget())
		return;
	glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
	glViewport(0, 0, DS_W, DS_H);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	/* The vertices arrive in DS screen coordinates with rows counting down, and a depth in -1..1
	 * with the far plane at +1 -- see G3SIM_AddVtx. glOrtho with a near of -1 and a far of +1 maps
	 * an eye z of -1 to the far plane, which is why the depth is handed over negated. */
	glOrtho(0, DS_W, DS_H, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);
	/* Transparent black: a pixel no polygon covered has to read back as "nothing here", so the 2D
	 * compositor shows the layer behind it. */
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void VitaGpuFrameEnd3D(void *rgbaOut)
{
	if (!ready || !targetReady)
		return;
	if (rgbaOut) {
		glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
		/* glReadPixels hands back rows from the bottom up, and the DS's count down the screen. */
		glReadPixels(0, 0, DS_W, DS_H, GL_RGBA, GL_UNSIGNED_BYTE, readback);
		for (unsigned row = 0; row < DS_H; row++)
			memcpy((unsigned char *)rgbaOut + row * DS_W * 4,
			       readback + (DS_H - 1 - row) * DS_W, DS_W * 4);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
}

unsigned VitaGpuTextureCreate(unsigned w, unsigned h, const void *rgba)
{
	GLuint texture = 0;

	if (!ready)
		return 0;
	glGenTextures(1, &texture);
	if (!texture)
		return 0;
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	textureBytes += w * h * 4;
	textureBlocks++;
	if (textureBytes > textureHigh)
		textureHigh = textureBytes;
	return texture;
}

void VitaGpuTextureDestroy(unsigned texture)
{
	GLuint name = texture;

	if (!ready || !texture)
		return;
	glDeleteTextures(1, &name);
	if (textureBlocks)
		textureBlocks--;
}

void VitaGpuDrawTriangles(unsigned texture, int repeatS, int repeatT,
                          const struct VitaGpuVertex *vertices, unsigned count)
{
	if (!ready || !targetReady || !count)
		return;
	glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
	if (texture) {
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeatS ? GL_REPEAT : GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeatT ? GL_REPEAT : GL_CLAMP_TO_EDGE);
		/* Modulate: the DS multiplies the texture by the vertex colour, which is where its
		 * lighting and its polygon alpha end up. */
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	} else {
		glDisable(GL_TEXTURE_2D);
	}
	if (polygonCull == VITAGPU_CULL_NONE) {
		glDisable(GL_CULL_FACE);
	} else {
		glEnable(GL_CULL_FACE);
		glCullFace(polygonCull == VITAGPU_CULL_FRONT ? GL_FRONT : GL_BACK);
	}
	glDepthFunc(polygonDepth == VITAGPU_DEPTH_LESS ? GL_LESS : GL_LEQUAL);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(3, GL_FLOAT, sizeof(struct VitaGpuVertex), &vertices[0].x);
	glTexCoordPointer(2, GL_FLOAT, sizeof(struct VitaGpuVertex), &vertices[0].u);
	glColorPointer(4, GL_FLOAT, sizeof(struct VitaGpuVertex), &vertices[0].r);
	glDrawArrays(GL_TRIANGLES, 0, count);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void VitaGpuMemStats(unsigned *bytes, unsigned *high, unsigned *blocks)
{
	if (bytes)
		*bytes = textureBytes;
	if (high)
		*high = textureHigh;
	if (blocks)
		*blocks = textureBlocks;
}

