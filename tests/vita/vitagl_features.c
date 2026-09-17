/* vitapoke: the vitaGL features the DS 2D compositor needs, in one translation unit.
 *
 * tests/vita/run.sh compiles and links this; nothing here runs. It is a contract check, not a test of
 * output: the renderer's design depends on three things vitaGL is not obliged to keep offering, and
 * losing any of them quietly would be expensive to discover later.
 *
 *   - Paletted textures (GL_COLOR_INDEX8_EXT with glColorTable). The DS stores backgrounds and sprites
 *     as 4bpp/8bpp tiles indexing a 16- or 256-entry palette, and the PSP's GE samples that format
 *     directly. GXM has P4 and P8 formats too, so the atlas design carries over instead of the port
 *     having to expand every tile to true colour each frame.
 *   - Stencil. The compositor builds the DS window and layer-priority masks in the stencil buffer.
 *   - Render to texture. The two DS engines and the 3D buffer are each composed into their own target
 *     and then drawn to the screen as textures.
 *
 * Everything else it touches -- alpha test, blend, scissor, colour mask, texture environment, the
 * fixed-function matrix stack -- is ordinary OpenGL 1.x and is exercised here for the same reason.
 */
#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>
#include <string.h>

/* The port's log, which shacccg_ext_stub.c writes to. Nothing here runs, so this only has to link. */
void VitaNativeMemLog(const char *fmt, ...) { (void)fmt; }
static unsigned char atlas[128*512];
static unsigned short clut[256];
int main(void) {
    vglInit(0x800000);
    GLuint tex, fb, fbtex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    /* paletted 8bpp atlas, the DS tile atlas format */
    glColorTable(GL_TEXTURE_2D, GL_RGBA, 256, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, clut);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 128, 512, 0,
                 GL_COLOR_INDEX, GL_UNSIGNED_BYTE, atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    /* render to texture, for compositing the 3D buffer */
    glGenTextures(1, &fbtex);
    glBindTexture(GL_TEXTURE_2D, fbtex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbtex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    /* the fixed-function state the GE backend sets per draw */
    glEnable(GL_STENCIL_TEST); glStencilFunc(GL_EQUAL, 1, 0xff); glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilMask(0xff); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.0f);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST); glScissor(0, 0, 960, 544);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 960, 544, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex);
    glBegin(GL_TRIANGLE_STRIP);
      glColor4f(1,1,1,1); glTexCoord2f(0,0); glVertex3f(0,0,0);
      glTexCoord2f(1,0); glVertex3f(363,0,0);
      glTexCoord2f(0,1); glVertex3f(0,272,0);
      glTexCoord2f(1,1); glVertex3f(363,272,0);
    glEnd();
    vglSwapBuffers(GL_FALSE);

    sceKernelExitProcess(0);
    return 0;
}
