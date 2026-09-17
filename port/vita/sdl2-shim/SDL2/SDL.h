/* vitapoke: see SDL_stdinc.h. */
#ifndef VITAPOKE_SDL_H
#define VITAPOKE_SDL_H
#include "SDL_stdinc.h"
#include "SDL_mutex.h"
#include "SDL_thread.h"
#include "SDL_timer.h"
#ifdef __cplusplus
extern "C" {
#endif
/* libntr's simulator header names this type in a struct member; nothing in the port touches audio
 * through SDL, so an opaque declaration is all it needs. */
typedef struct SDL_AudioStream SDL_AudioStream;
/* Reached only by libntr modules the SDK filter drops (see SDL_stdinc.h). */
#define SDL_MESSAGEBOX_ERROR       0x00000010u
#define SDL_MESSAGEBOX_WARNING     0x00000020u
#define SDL_MESSAGEBOX_INFORMATION 0x00000040u
int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, void *window);
#ifdef __cplusplus
}
#endif
#endif
