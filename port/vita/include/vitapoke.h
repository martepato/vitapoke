/* vitapoke: constants and entry points specific to the Vita build.
 *
 * It exists so the Vita-only pieces -- the display layout, the renderer, the audio backend, the file
 * paths -- agree on the numbers and the names without each inventing its own.
 */
#ifndef VITAPOKE_H
#define VITAPOKE_H

/* The Vita's display. 960x544 against the PSP's 480x272: exactly four times the area, which is what
 * makes a comfortable layout possible where the PSP had to squeeze.
 */
#define VITAPOKE_SCREEN_W 960
#define VITAPOKE_SCREEN_H 544

/* A DS screen. */
#define VITAPOKE_DS_W 256
#define VITAPOKE_DS_H 192

/* Layout of the two DS panels on the Vita display.
 *
 * Both screens get an integer 2x scale (512x384) and neither is shrunk into a corner, which is what the
 * PSP layout had to do to fit 480x272. Stacking them needs 768 rows and there are only 544, so they sit
 * side by side with the touch screen on the right, where the hands are: 1024 columns is 64 too many, so
 * the pair is centred and the 32-column overlap is taken off the outer edges. The gap between them
 * lines up with the console's centre line.
 *
 * This is the starting layout, not the final one: see docs/VITA.md. The renderer is the only reader,
 * so an alternative (one screen large with the other small, as the PSP had to do) is a change here
 * and nowhere else.
 */
#define VITAPOKE_MAIN_SCALE 2
#define VITAPOKE_MAIN_W (VITAPOKE_DS_W * VITAPOKE_MAIN_SCALE)
#define VITAPOKE_MAIN_H (VITAPOKE_DS_H * VITAPOKE_MAIN_SCALE)
#define VITAPOKE_MAIN_X 0
#define VITAPOKE_MAIN_Y ((VITAPOKE_SCREEN_H - VITAPOKE_MAIN_H) / 2)
#define VITAPOKE_SUB_W VITAPOKE_MAIN_W
#define VITAPOKE_SUB_H VITAPOKE_MAIN_H
#define VITAPOKE_SUB_X (VITAPOKE_SCREEN_W - VITAPOKE_SUB_W)
#define VITAPOKE_SUB_Y VITAPOKE_MAIN_Y

/* Where the game's own files live on the memory card.
 *
 * A Vita application cannot write beside its own executable the way a PSP homebrew could: ux0:app is
 * mounted read-only while the application runs. So the ROM, the save and the log all live in one
 * directory under ux0:data that the user owns and can copy files in and out of.
 */
#define VITAPOKE_DATA_DIR  "ux0:data/vitapoke"
#define VITAPOKE_LOG_PATH  VITAPOKE_DATA_DIR "/log.txt"
#define VITAPOKE_ROM_PATH  VITAPOKE_DATA_DIR "/Platinum.nds"
#define VITAPOKE_SAVE_PATH VITAPOKE_DATA_DIR "/Platinum.sav"

#endif /* VITAPOKE_H */
