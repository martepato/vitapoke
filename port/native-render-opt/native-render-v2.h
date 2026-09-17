#ifndef VITAPOKE_RENDER_H
#define VITAPOKE_RENDER_H
#ifdef __cplusplus
extern "C" {
#endif
/* Begin before native G3 commands. Present waits for GPU, composites both DS
 * engines, presents physical screens, and closes the frame. Return0 succeeds;
 * negative means unsupported capture/FIFO/state and caller should stop. */
int VitaNativeRenderInit(void);
int VitaNativeRenderBegin(void);
int VitaNativeRenderPresent(void);
int VitaNativeRenderPresentNoWait(void); /* Game already owns frame pacing. */
void VitaNativeRenderSetInput(unsigned keys,int touchMode,int down,int x,int y);
void VitaNativeRenderShutdown(void);
void VitaNativeRenderGetTimings(unsigned *readbackUs,unsigned *software2DUs);
unsigned VitaNativeRenderLastDrawMask(void); /* engineA bit0, engineB bit1 */
unsigned VitaNativeRenderFrameCount(void);
#ifdef __cplusplus
}
#endif
#endif
