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
 * The two screens sit side by side, the touch screen on the right where the hands are. They cannot
 * be stacked: two 2x screens need 768 rows and there are 544.
 *
 * Side by side, the scale is decided for us. 960 columns for two screens is 480 each, and 480 is
 * 256 * 15/8 -- so each panel is 480x360, which is exactly 4:3, exactly what the DS's 256x192 is,
 * and exactly half the display's width. Nothing is cropped, nothing overlaps, and 92 rows of black
 * sit above and below.
 *
 * It is not an integer scale, which is the one thing wrong with it: at 15/8 some source pixels
 * cover two display pixels and some cover one. The panels are therefore drawn with bilinear
 * filtering, which turns that from a visible pattern of uneven columns into a slight softness. An
 * integer 2x is what this layout tried first, and it does not fit: 1024 columns is 64 too many, and
 * drawing both panels full size just puts the touch screen on top of the main screen's right-hand
 * 64 columns, which is where the main screen's own interface often is. Losing a sixteenth of both
 * screens to a sharper picture is the wrong trade for a game whose menus reach the edges.
 *
 * The renderer is the only reader of these, so an alternative layout (one screen large with the
 * other small, as the PSP had to do) is a change here and nowhere else. If you change them,
 * port/native-vita-render/render-vita.cpp has static assertions that keep the GPU layer's own copy
 * of the numbers honest, and port/vita/input.c maps the front panel through the VITAPOKE_SUB_*
 * rectangle, so touch follows automatically.
 */
#define VITAPOKE_MAIN_W (VITAPOKE_SCREEN_W / 2)
#define VITAPOKE_MAIN_H (VITAPOKE_MAIN_W * VITAPOKE_DS_H / VITAPOKE_DS_W)
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
/* Written beside the executable by a build that unpacked a ROM into the VPK (see
 * scripts/extract_assets.py). Its presence is what tells the port the data is already here. app0:
 * is the application's own directory, and read only, which is all the game's data needs. */
#define VITAPOKE_ASSET_INDEX "app0:nitrofs.idx"
#define VITAPOKE_SAVE_PATH VITAPOKE_DATA_DIR "/Platinum.sav"

#endif /* VITAPOKE_H */
