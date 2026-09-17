/* Cooperative 60Hz SDK VBlank deadlines. Presentation still uses PSP vsync. */
#include <pspkernel.h>
#include <stdint.h>
static uint64_t epoch,sequence,idleUs;static int ready;
/* Time spent sleeping for a VBlank deadline; the frame profiler subtracts it from the game window. */
unsigned long long VitaNativeVBlankIdleTake(void){uint64_t v=idleUs;idleUs=0;return v;}
void VitaNativeVBlankReset(void){ready=0;}
static uint64_t deadline(void){return epoch+(sequence*1000000u)/60u;}
void VitaNativeVBlankWait(void){uint64_t now=sceKernelGetSystemTimeWide();if(!ready){epoch=now;sequence=1;ready=1;}uint64_t target=deadline();if(now<target)idleUs+=target-now;while(now<target){uint64_t diff=target-now;unsigned part=diff>1000000?1000000:(unsigned)diff;sceKernelDelayThread(part);now=sceKernelGetSystemTimeWide();}sequence++;}
/* Bound recovery after sustained over-budget work to one game update. Do this
 * once per completed game frame, not between its two SDK VBlank callbacks. */
void VitaNativeVBlankFrameComplete(void){if(!ready)return;uint64_t now=sceKernelGetSystemTimeWide();if(now>deadline()+33334u){epoch=now-16667u;sequence=0;}}
