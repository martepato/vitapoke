/* vitapoke: see SDL_stdinc.h. */
#ifndef VITAPOKE_SDL_MUTEX_H
#define VITAPOKE_SDL_MUTEX_H
#include "SDL_stdinc.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_sem SDL_sem;
SDL_mutex *SDL_CreateMutex(void);
int SDL_LockMutex(SDL_mutex *mutex);
int SDL_TryLockMutex(SDL_mutex *mutex);
int SDL_UnlockMutex(SDL_mutex *mutex);
void SDL_DestroyMutex(SDL_mutex *mutex);
SDL_sem *SDL_CreateSemaphore(Uint32 initial);
int SDL_SemWait(SDL_sem *sem);
int SDL_SemWaitTimeout(SDL_sem *sem, Uint32 ms);
int SDL_SemPost(SDL_sem *sem);
void SDL_DestroySemaphore(SDL_sem *sem);
#ifdef __cplusplus
}
#endif
#endif
