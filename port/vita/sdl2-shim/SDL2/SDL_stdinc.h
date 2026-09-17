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
 *
 * Some of what is declared here is deliberately never implemented. libntr's own os_thread.c,
 * os_mutex.c, os_alarm.c, os_tick.c, os_message.c and fs_file.c are SDL-based implementations of DS
 * services that this port replaces with its own (port/vita/os_*.c), and the SDK archive filter drops
 * those modules before the link. They still have to *compile*, so their SDL calls need declarations,
 * but nothing ever calls them and no definition is provided. A link error naming one of these is a
 * sign the filter stopped dropping a module it used to.
 */
#ifndef VITAPOKE_SDL_STDINC_H
#define VITAPOKE_SDL_STDINC_H
#include <stdint.h>
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int32_t Sint32;
#endif
