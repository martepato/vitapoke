#include <nitro.h>
#include <pspctrl.h>
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static TPData point={128,96,0,TP_VALIDITY_VALID},sample;
static unsigned rtcReads;
static int touchMode;static unsigned previous;static BOOL sampling,rtcReady;
static TPCalibrateParam calibration;
static const unsigned buttons[12]={PSP_CTRL_CIRCLE,PSP_CTRL_CROSS,PSP_CTRL_SELECT,PSP_CTRL_START,PSP_CTRL_RIGHT,PSP_CTRL_LEFT,PSP_CTRL_UP,PSP_CTRL_DOWN,PSP_CTRL_RTRIGGER,PSP_CTRL_LTRIGGER,PSP_CTRL_TRIANGLE,PSP_CTRL_SQUARE};
void VitaNativeInputStep(unsigned bits,unsigned ax,unsigned ay){
 unsigned combo=PSP_CTRL_SELECT|PSP_CTRL_CROSS,keys=0;
 if((bits&combo)==combo&&(previous&combo)!=combo)touchMode=!touchMode;previous=bits;
 for(unsigned i=0;i<12;i++)if(bits&buttons[i])keys|=1u<<i;
 int x=point.x,y=point.y;
 if(touchMode){keys&=~(PAD_BUTTON_B|PAD_PLUS_KEY_MASK);if(bits&PSP_CTRL_LEFT)x-=3;if(bits&PSP_CTRL_RIGHT)x+=3;if(bits&PSP_CTRL_UP)y-=3;if(bits&PSP_CTRL_DOWN)y+=3;}
 if(ax<40)x-=2;else if(ax>215)x+=2;if(ay<40)y-=2;else if(ay>215)y+=2;
 point.x=x<0?0:x>255?255:x;point.y=y<0?0:y>191?191:y;
 point.touch=touchMode&&(bits&PSP_CTRL_CROSS)?1:0;point.validity=TP_VALIDITY_VALID;
 s_reg_PAD_KEYINPUT=(~keys)&PAD_KEYPORT_MASK;
 *(vu16*)(uintptr_t)HW_BUTTON_XY_BUF=(~keys)&PAD_RCNTPORT_MASK; // PSP has no closing DS lid.
}
void VitaNativeInputPoll(void){SceCtrlData pad;if(sceCtrlPeekBufferPositive(&pad,1)<0){printf("[INPUT] PSP pad read failed\n");abort();}VitaNativeInputStep(pad.Buttons,pad.Lx,pad.Ly);}
void TP_Init(void){sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);touchMode=0;previous=0;sampling=FALSE;point=(TPData){128,96,0,TP_VALIDITY_VALID};VitaNativeInputStep(0,128,128);}
BOOL TP_GetUserInfo(TPCalibrateParam*p){if(!p)return FALSE;*p=(TPCalibrateParam){0,0,256,256};return TRUE;}
void TP_SetCalibrateParam(const TPCalibrateParam*p){if(!p||p->x0||p->y0||p->xDotSize!=256||p->yDotSize!=256){printf("[INPUT] unsupported non-identity touch calibration\n");abort();}calibration=*p;}
void TP_GetCalibratedPoint(TPData*out,const TPData*raw){if(!out||!raw)abort();*out=*raw;}
void TP_RequestSamplingAsync(void){sample=point;sampling=TRUE;}
u32 TP_WaitRawResult(TPData*out){if(!out||!sampling)return TP_RESULT_ILLEGAL_STATUS;*out=sample;sampling=FALSE;return TP_RESULT_SUCCESS;}
void TP_GetLatestRawPointInAuto(TPData*out){(void)out;printf("[INPUT] unsupported automatic touch sampler\n");abort();}
void RTC_Init(void){rtcReady=TRUE;}
RTCResult RTC_GetDateTimeAsync(RTCDate*d,RTCTime*t,RTCCallback cb,void*arg){
 if(!rtcReady)return RTC_RESULT_ILLEGAL_STATUS;if(!d||!t||!cb)return RTC_RESULT_ILLEGAL_PARAMETER;
 ScePspDateTime now;if(sceRtcGetCurrentClockLocalTime(&now)<0)return RTC_RESULT_FATAL_ERROR;
 if(now.year<2000||now.year>2099)return RTC_RESULT_ILLEGAL_PARAMETER;
 *d=(RTCDate){now.year-2000,now.month,now.day,sceRtcGetDayOfWeek(now.year,now.month,now.day)};
 *t=(RTCTime){now.hour,now.minute,now.second};rtcReads++;cb(RTC_RESULT_SUCCESS,arg);return RTC_RESULT_SUCCESS;
}

unsigned VitaNativeRTCReadCount(void){return rtcReads;}
int VitaNativeInputQuitRequested(void){unsigned combo=PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SELECT;return (previous&combo)==combo;}

void VitaNativeInputGetRenderState(unsigned*keys,int*mode,int*down,int*x,int*y){
 if(keys)*keys=PAD_Read();if(mode)*mode=touchMode;if(down)*down=point.touch;if(x)*x=point.x;if(y)*y=point.y;
}
