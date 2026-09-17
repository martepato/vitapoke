/* vitapoke: the sliver of SDL2 that libntr's headers and libraries refer to.
 *
 * libntr is built for desktop simulators as well as real DS hardware, and on any target that is not
 * SDK_BUILD_ARM its OS headers reach for SDL2 for three types (SDL_mutex, SDL_sem, SDL_TimerID) and its
 * libraries call SDL's mutex, thread and delay functions. The PSP build satisfied that with PSPDEV's
 * real SDL2 headers plus a small adapter over the PSP kernel; VitaSDK ships no SDL2 at all, and pulling
 * in a real one to borrow five declarations would be absurd.
 *
 * So these headers declare exactly what libntr refers to and nothing else, and port/vita/sdl_sync.c
 * implements them on psp2 threads. This is not an SDL port and must not grow into one: anything that
 * needs more of SDL than libntr does should use psp2 directly.
 */
#ifndef VITAPOKE_SDL_STDINC_H
#define VITAPOKE_SDL_STDINC_H
#include <stdint.h>
typedef uint32_t Uint32;
typedef int32_t Sint32;
#endif
