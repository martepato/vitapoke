/* vitapoke: see SDL_stdinc.h. */
#ifndef VITAPOKE_SDL_TIMER_H
#define VITAPOKE_SDL_TIMER_H
#include "SDL_stdinc.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef int SDL_TimerID;
void SDL_Delay(Uint32 ms);
Uint32 SDL_GetTicks(void);
#ifdef __cplusplus
}
#endif
#endif
