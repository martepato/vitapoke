/* vitapoke: see SDL_stdinc.h. */
#ifndef VITAPOKE_SDL_THREAD_H
#define VITAPOKE_SDL_THREAD_H
#include "SDL_stdinc.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SDL_Thread SDL_Thread;
typedef int (*SDL_ThreadFunction)(void *data);
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data);
void SDL_WaitThread(SDL_Thread *thread, int *status);
#ifdef __cplusplus
}
#endif
#endif
