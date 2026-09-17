/* vitapoke: see SDL_stdinc.h. */
#ifndef VITAPOKE_SDL_TIMER_H
#define VITAPOKE_SDL_TIMER_H
#include "SDL_stdinc.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef int SDL_TimerID;
typedef Uint32 (*SDL_TimerCallback)(Uint32 interval, void *param);
void SDL_Delay(Uint32 ms);
Uint32 SDL_GetTicks(void);
Uint64 SDL_GetTicks64(void);
SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *param);
int SDL_RemoveTimer(SDL_TimerID id);
#ifdef __cplusplus
}
#endif
#endif
