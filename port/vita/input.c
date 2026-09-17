/* vitapoke: the DS's pad, touch screen and clock on the Vita.
 *
 * The DS interfaces the game calls are TP_* for the touch panel, RTC_* for the clock, and the two pad
 * registers it reads directly. This implements them on psp2.
 *
 * The touch screen is the reason this file is not the PSP one. The PSP has no touch panel, so that
 * build draws a cursor, moves it with the analogue stick and taps with L -- and gives up the DS R
 * button to toggle the mode. The Vita has a front touch panel, so a touch on the DS touch screen is a
 * touch: TP_* reports where the finger is, all the buttons keep their DS meanings, and there is no
 * mode to be in.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <nitro.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/rtc.h>

#include "include/vita_os.h"
#include "include/vita_input.h"
#include "include/vitapoke.h"

/* The application's keyboard, when one is on screen, owns the input: the game behind it must see no
 * buttons at all, or the presses meant for the keyboard also drive whatever is suspended underneath.
 * Weak so this file links into builds that have no keyboard yet. */
int VitaNativeOskIsActive(void) __attribute__((weak));

/* sceTouch reports the front panel in its own units, twice the display resolution in each axis. */
#define TOUCH_MAX_X 1920
#define TOUCH_MAX_Y 1088

static TPData point = { 128, 96, 0, TP_VALIDITY_VALID };
static TPData sample;
static BOOL sampling, rtcReady;
static unsigned rtcReads;
static unsigned heldButtons;
static TPCalibrateParam calibration;

/* The DS button order in the pad register. Identical bit values on the PSP and the Vita, so the table
 * is the mapping itself. */
static const unsigned dsButtons[12] = {
	SCE_CTRL_CIRCLE, SCE_CTRL_CROSS,    SCE_CTRL_SELECT,   SCE_CTRL_START,
	SCE_CTRL_RIGHT,  SCE_CTRL_LEFT,     SCE_CTRL_UP,       SCE_CTRL_DOWN,
	SCE_CTRL_RTRIGGER, SCE_CTRL_LTRIGGER, SCE_CTRL_TRIANGLE, SCE_CTRL_SQUARE,
};

/* Map a front-panel touch to DS touch-screen coordinates. Returns false when the touch is outside the
 * panel the touch screen is drawn on, which is most of the display: the DS touch screen occupies only
 * its own rectangle (see include/vitapoke.h), and a finger elsewhere is not a stylus press. */
static BOOL TouchToDS(const SceTouchReport *report, int *outX, int *outY)
{
	int x = (int)report->x * VITAPOKE_SCREEN_W / TOUCH_MAX_X;
	int y = (int)report->y * VITAPOKE_SCREEN_H / TOUCH_MAX_Y;

	x -= VITAPOKE_SUB_X;
	y -= VITAPOKE_SUB_Y;
	if (x < 0 || y < 0 || x >= VITAPOKE_SUB_W || y >= VITAPOKE_SUB_H)
		return FALSE;

	*outX = x * VITAPOKE_DS_W / VITAPOKE_SUB_W;
	*outY = y * VITAPOKE_DS_H / VITAPOKE_SUB_H;
	if (*outX > VITAPOKE_DS_W - 1)
		*outX = VITAPOKE_DS_W - 1;
	if (*outY > VITAPOKE_DS_H - 1)
		*outY = VITAPOKE_DS_H - 1;
	return TRUE;
}

/* Fold one frame of pad and touch state into the DS registers the game reads. */
void VitaNativeInputStep(unsigned buttons, const SceTouchData *touch)
{
	unsigned keys = 0;

	heldButtons = buttons;
	for (unsigned i = 0; i < 12; i++)
		if (buttons & dsButtons[i])
			keys |= 1u << i;

	if (touch && touch->reportNum > 0) {
		int x, y;
		/* First report that lands on the touch screen wins. The DS panel is single-touch, so
		 * there is nothing sensible to do with a second finger. */
		for (unsigned i = 0; i < touch->reportNum; i++) {
			if (TouchToDS(&touch->report[i], &x, &y)) {
				point.x = (u16)x;
				point.y = (u16)y;
				point.touch = 1;
				break;
			}
		}
		if (!point.touch)
			point.touch = 0;
	} else if (point.touch) {
		/* Finger lifted. The DS keeps reporting the last position with touch clear, which is what
		 * the game's own release handling expects. */
		point.touch = 0;
	}
	point.validity = TP_VALIDITY_VALID;

	s_reg_PAD_KEYINPUT = (~keys) & PAD_KEYPORT_MASK;
	/* The Vita has no lid, so the hinge bit in the extended pad register stays clear. */
	*(vu16 *)(uintptr_t)HW_BUTTON_XY_BUF = (~keys) & PAD_RCNTPORT_MASK;
}

void VitaNativeInputPoll(void)
{
	SceCtrlData pad;
	SceTouchData touch;

	if (VitaNativeOskIsActive && VitaNativeOskIsActive()) {
		VitaNativeInputStep(0, NULL);
		return;
	}

	memset(&pad, 0, sizeof pad);
	memset(&touch, 0, sizeof touch);
	if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0)
		VitaNativeFatal("pad read failed");
	/* A touch read that fails is not fatal: report no touch and keep the buttons working. */
	if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) < 0)
		touch.reportNum = 0;

	VitaNativeInputStep(pad.buttons, &touch);
}

/* ---------------------------------------------------------------- TP: the DS touch panel */

void TP_Init(void)
{
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	/* The rear panel is not used: nothing in either game wants a second pointer, and leaving it off
	 * saves the sampling. */
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_STOP);
	heldButtons = 0;
	sampling = FALSE;
	point = (TPData){ 128, 96, 0, TP_VALIDITY_VALID };
	VitaNativeInputStep(0, NULL);
}

BOOL TP_GetUserInfo(TPCalibrateParam *param)
{
	if (!param)
		return FALSE;
	/* The touch is already mapped to DS coordinates, so the calibration the game reads is the
	 * identity. */
	*param = (TPCalibrateParam){ 0, 0, 256, 256 };
	return TRUE;
}

void TP_SetCalibrateParam(const TPCalibrateParam *param)
{
	if (!param || param->x0 || param->y0 || param->xDotSize != 256 || param->yDotSize != 256)
		VitaNativeFatal("TP_SetCalibrateParam: unsupported non-identity calibration");
	calibration = *param;
}

void TP_GetCalibratedPoint(TPData *out, const TPData *raw)
{
	if (!out || !raw)
		VitaNativeFatal("TP_GetCalibratedPoint: null argument");
	*out = *raw;
}

void TP_RequestSamplingAsync(void)
{
	sample = point;
	sampling = TRUE;
}

u32 TP_WaitRawResult(TPData *out)
{
	if (!out || !sampling)
		return TP_RESULT_ILLEGAL_STATUS;
	*out = sample;
	sampling = FALSE;
	return TP_RESULT_SUCCESS;
}

/* The DS can have the hardware sample the panel by itself into a ring buffer, several times per frame.
 * Here the ring is filled from the last polled position once a frame, which is as often as the panel
 * is read. */
static TPData *autoBuffer;
static u16 autoCount, autoFrequency, autoIndex;
static u32 autoError;

void TP_RequestAutoSamplingStartAsync(u16 line, u16 frequency, TPData *buffer, u16 count)
{
	if (autoBuffer) {
		autoError = TP_RESULT_ILLEGAL_STATUS;
		return;
	}
	if (line >= 263 || !frequency || frequency > 16 || !buffer || !count) {
		autoError = TP_RESULT_INVALID_PARAMETER;
		return;
	}
	autoBuffer = buffer;
	autoCount = count;
	autoFrequency = frequency;
	autoIndex = 0;
	autoError = 0;
	for (u16 i = 0; i < count; i++)
		autoBuffer[i] = (TPData){ 0, 0, 0, TP_VALIDITY_VALID };
}

void TP_RequestAutoSamplingStopAsync(void)
{
	if (!autoBuffer) {
		autoError = TP_RESULT_ILLEGAL_STATUS;
		return;
	}
	autoBuffer = NULL;
	autoError = 0;
}

void TP_WaitBusy(TPRequestCommandFlag flags) { (void)flags; /* reads complete synchronously here */ }
u32 TP_CheckError(TPRequestCommandFlag flags) { (void)flags; return autoError; }
u16 TP_GetLatestIndexInAuto(void) { return autoIndex; }

void TP_GetLatestRawPointInAuto(TPData *out)
{
	if (!out || !autoBuffer)
		VitaNativeFatal("TP_GetLatestRawPointInAuto: no auto sampling in progress");
	*out = autoBuffer[autoIndex];
}

void VitaNativeInputVBlank(void)
{
	if (!autoBuffer)
		return;
	for (u16 i = 0; i < autoFrequency; i++) {
		autoIndex = (u16)((autoIndex + 1) % autoCount);
		autoBuffer[autoIndex] = point;
	}
}

/* ---------------------------------------------------------------- RTC: the DS clock */

void RTC_Init(void) { rtcReady = TRUE; }

RTCResult RTC_GetDateTimeAsync(RTCDate *date, RTCTime *time, RTCCallback callback, void *arg)
{
	SceDateTime now;

	if (!rtcReady)
		return RTC_RESULT_ILLEGAL_STATUS;
	if (!date || !time || !callback)
		return RTC_RESULT_ILLEGAL_PARAMETER;

	memset(&now, 0, sizeof now);
	if (sceRtcGetCurrentClockLocalTime(&now) < 0)
		return RTC_RESULT_FATAL_ERROR;
	/* The DS stores a two-digit year from 2000. A console clock outside that range would silently
	 * wrap the game's date arithmetic, so refuse instead. */
	if (now.year < 2000 || now.year > 2099)
		return RTC_RESULT_ILLEGAL_PARAMETER;

	*date = (RTCDate){ now.year - 2000, now.month, now.day,
	                   sceRtcGetDayOfWeek(now.year, now.month, now.day) };
	*time = (RTCTime){ now.hour, now.minute, now.second };
	rtcReads++;
	callback(RTC_RESULT_SUCCESS, arg);
	return RTC_RESULT_SUCCESS;
}

static void StoreResult(RTCResult value, void *p) { *(RTCResult *)p = value; }

RTCResult RTC_GetDateTime(RTCDate *date, RTCTime *time)
{
	RTCResult result = RTC_RESULT_FATAL_ERROR;
	RTCResult request = RTC_GetDateTimeAsync(date, time, StoreResult, &result);
	return request == RTC_RESULT_SUCCESS ? result : request;
}

RTCResult RTC_GetDate(RTCDate *date)
{
	RTCTime time;
	return RTC_GetDateTime(date, &time);
}

RTCResult RTC_GetTime(RTCTime *time)
{
	RTCDate date;
	return RTC_GetDateTime(&date, time);
}

unsigned VitaNativeRTCReadCount(void) { return rtcReads; }

/* ---------------------------------------------------------------- the renderer's view of input
 *
 * The renderer draws the touch position so the player can see where a press landed. There is no stylus
 * mode on the Vita, so the mode flag the PSP passed is gone; what the renderer needs is where the
 * finger is and whether it is down.
 */
void VitaNativeInputGetRenderState(unsigned *keys, int *down, int *x, int *y)
{
	if (keys)
		*keys = PAD_Read();
	if (down)
		*down = point.touch;
	if (x)
		*x = point.x;
	if (y)
		*y = point.y;
}

/* The PSP build quits with L+R+SELECT because its firmware gives a homebrew application no other way
 * out. The Vita's PS button suspends and closes the application, so there is no combination to
 * reserve and nothing here steals a DS button. */
