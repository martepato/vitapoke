/* vitapoke: what the Vita's input layer offers the rest of the port.
 *
 * The DS interfaces (TP_*, RTC_*, the pad registers) are declared by libntr and implemented in
 * port/vita/input.c. These are the extra entry points the application and the renderer need: the
 * per-frame poll, the vblank tick for the DS's automatic sampling, and where the finger is so the
 * renderer can draw it.
 */
#ifndef VITAPOKE_VITA_INPUT_H
#define VITAPOKE_VITA_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read the pad and the touch panel and fold them into the DS registers. Called once a frame. */
void VitaNativeInputPoll(void);

/* Advance the DS's automatic touch sampling ring by one frame's worth of readings. */
void VitaNativeInputVBlank(void);

/* Where the touch is, for the renderer's on-screen marker. `down` is whether a finger is on the
 * DS touch screen; `x` and `y` are in DS coordinates. There is no stylus mode on the Vita, so unlike
 * the PSP build there is no mode to report. */
void VitaNativeInputGetRenderState(unsigned *keys, int *down, int *x, int *y);

/* How many times the game has read the clock. Used by the startup log. */
unsigned VitaNativeRTCReadCount(void);

/* True while an alarm handler is running, so os_core.c can report OS_PROCMODE_IRQ as the DS would
 * inside a timer interrupt. Implemented in os_alarm.c. */
int VitaNativeAlarmInCallback(void);

#ifdef __cplusplus
}
#endif
#endif /* VITAPOKE_VITA_INPUT_H */
