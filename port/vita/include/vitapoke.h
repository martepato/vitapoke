/* vitapoke: constants and entry points specific to the Vita build.
 *
 * Nothing in port/ outside port/vita includes this; the shared sources keep their PSP spelling and the
 * compat layer translates. It exists so the Vita-only pieces -- the display layout, the sceGu shim, the
 * audio backend -- agree on the numbers without each inventing its own.
 */
#ifndef VITAPOKE_H
#define VITAPOKE_H

/* The Vita's display. 960x544 against the PSP's 480x272: exactly four times the area, which is what
 * makes a comfortable layout possible where the PSP had to squeeze.
 */
#define PSPOKE_VITA_SCREEN_W 960
#define PSPOKE_VITA_SCREEN_H 544

/* A DS screen. */
#define PSPOKE_DS_W 256
#define PSPOKE_DS_H 192

/* Layout of the two DS panels on the Vita display.
 *
 * Both screens get an integer 2x scale (512x384) and neither is shrunk into a corner, which is what the
 * PSP layout had to do to fit 480x272. Stacking them needs 768 rows and there are only 544, so they sit
 * side by side with the touch screen on the right, where the hands are: 1024 columns is 64 too many, so
 * the pair is centred and the 32-column overlap is taken off the outer edges. The gap between them
 * lines up with the console's centre line.
 *
 * This is the starting layout, not the final one: see docs/VITA.md. The renderer reads these, so an
 * alternative (one screen at 2.8x with the other small, as on the PSP) is a change here plus the
 * matching change in port/vita/gu.
 */
#define PSPOKE_MAIN_SCALE 2
#define PSPOKE_MAIN_W (PSPOKE_DS_W * PSPOKE_MAIN_SCALE)
#define PSPOKE_MAIN_H (PSPOKE_DS_H * PSPOKE_MAIN_SCALE)
#define PSPOKE_MAIN_X 0
#define PSPOKE_MAIN_Y ((PSPOKE_VITA_SCREEN_H - PSPOKE_MAIN_H) / 2)
#define PSPOKE_SUB_W PSPOKE_MAIN_W
#define PSPOKE_SUB_H PSPOKE_MAIN_H
#define PSPOKE_SUB_X (PSPOKE_VITA_SCREEN_W - PSPOKE_SUB_W)
#define PSPOKE_SUB_Y PSPOKE_MAIN_Y

#endif /* VITAPOKE_H */
