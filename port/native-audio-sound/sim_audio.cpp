#include <nitro.h>
#include <simulator/sim_audio.h>
#include <simulator/sim.h>
#include <simulator/config/sim_config.h>
#include <SDL2/SDL.h>

#include "blip_buf.h"

#ifdef SDK_TRACY_ENABLE
#include "tracy/TracyC.h"
#endif

#ifdef VITAPOKE_SIM_AUDIO_DEVICE
static SDL_AudioSpec s_requestedAudioSpec, s_actualAudioSpec;
static SDL_AudioDeviceID s_audioDevice;
#endif

u32 s_SIM_sndcnt[16] = {0};
u8 * s_SIM_sndsad[16] = {0};
u16 s_SIM_sndtmr[16] = {0};
u16 s_SIM_sndpnt[16] = {0};
u32 s_SIM_sndlen[16] = {0};

static s32 s_SIM_internalSoundTimer[16] = {0};
static s32 s_SIM_internalSoundPos[16] = {0};
static s32 s_SIM_internalSoundSample[16] = {0};
static s32 s_SIM_internalADPCMValLoop[16] = {0};
static s32 s_SIM_internalADPCMIndexLoop[16] = {0};
static s32 s_SIM_internalADPCMVal[16] = {0};
static s32 s_SIM_internalADPCMIndex[16] = {0};
static u8 s_SIM_internalADPCMCurByte[16] = {0};
static s32 s_SIM_internalNextADPCMByte[16] = {0};
static u16 s_SIM_internalNoiseVal[16] = {0};

static int s_blipTimer = 0;
static blip_t* s_BlipLeft;
static blip_t* s_BlipRight;
static s16* s_outputBuffer;
static u32 s_outputBufferWritePos = 0;
static u32 s_outputBufferReadPos = 0;

static int s_outputLastLeftSample;
static int s_outputLastRightSample;

static const u16 s_ADPCMTable[89] =
{
    0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E,
    0x0010, 0x0011, 0x0013, 0x0015, 0x0017, 0x0019, 0x001C, 0x001F,
    0x0022, 0x0025, 0x0029, 0x002D, 0x0032, 0x0037, 0x003C, 0x0042,
    0x0049, 0x0050, 0x0058, 0x0061, 0x006B, 0x0076, 0x0082, 0x008F,
    0x009D, 0x00AD, 0x00BE, 0x00D1, 0x00E6, 0x00FD, 0x0117, 0x0133,
    0x0151, 0x0173, 0x0198, 0x01C1, 0x01EE, 0x0220, 0x0256, 0x0292,
    0x02D4, 0x031C, 0x036C, 0x03C3, 0x0424, 0x048E, 0x0502, 0x0583,
    0x0610, 0x06AB, 0x0756, 0x0812, 0x08E0, 0x09C3, 0x0ABD, 0x0BD0,
    0x0CFF, 0x0E4C, 0x0FBA, 0x114C, 0x1307, 0x14EE, 0x1706, 0x1954,
    0x1BDC, 0x1EA5, 0x21B6, 0x2515, 0x28CA, 0x2CDF, 0x315B, 0x364B,
    0x3BB9, 0x41B2, 0x4844, 0x4F7E, 0x5771, 0x602F, 0x69CE, 0x7462,
    0x7FFF
};

static const s8 s_ADPCMIndexTable[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static const s16 s_PSGTable[8][8] =
{
    {-0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF,  0x7FFF},
    {-0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF,  0x7FFF,  0x7FFF},
    {-0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF},
    {-0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF},
    {-0x7FFF, -0x7FFF, -0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF},
    {-0x7FFF, -0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF},
    {-0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF,  0x7FFF},
    {-0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF, -0x7FFF}
};

#define INTERNAL_SAMPLE_RATE 16756991.f


/* The desktop simulator's audio device, and the resampler that feeds it.
 *
 * Neither is used on a console. This port takes the mixer's own output instead -- see
 * VitaNativeAudioMixBlock further down -- and plays it through sceAudioOut at the DS's rate, with no
 * resampling in software at all. These two functions are the SDL path they replace: kept as the
 * reference for what the simulator does, compiled only where there is an SDL audio device to open.
 */
static u8 GetNextADPCMByte(int chNo);
static void PanOutput(s32 in, s32 * left, s32 * right, int chNo);

#ifdef VITAPOKE_SIM_AUDIO_DEVICE
void SIM_Audio_Init(int aAudioFrequency)
{
    s_BlipLeft = blip_new(512*64);
    s_BlipRight = blip_new(512*64);

    blip_set_rates(s_BlipLeft, INTERNAL_SAMPLE_RATE * 1.0f, aAudioFrequency);
    blip_set_rates(s_BlipRight, INTERNAL_SAMPLE_RATE * 1.0f, aAudioFrequency);

    memset(&s_requestedAudioSpec, 0, sizeof(SDL_AudioSpec));
    s_requestedAudioSpec.freq = aAudioFrequency;
    s_requestedAudioSpec.format = AUDIO_S16LSB;
    s_requestedAudioSpec.channels = 2;

    // Set up the number of samples so NitroComposer runs at the exact right interval
    s_requestedAudioSpec.samples = (int)((double)aAudioFrequency * 0.005215419);
    if(s_requestedAudioSpec.samples & 1) {
        s_requestedAudioSpec.samples += 1;
    }
    s_requestedAudioSpec.callback = SIM_Audio_Callback;
    if(SDL_OpenAudio(&s_requestedAudioSpec, NULL) < 0) {
        printf("SDL Error %s\n", SDL_GetError());
    }
    SDL_PauseAudio(0);
}

void SIM_Audio_Callback(void *userdata, Uint8 *stream, int len)
{
    #ifdef SDK_TRACY_ENABLE
    TracyCZone(ctx, 1);
    #endif
    // Run NitroComposer
    SND_UpdateExChannel();
    SND_SeqMain(TRUE);
    SND_ExChannelMain(TRUE);

    // get samples from each channel and do the thing
    

    while(blip_samples_avail(s_BlipLeft) < len / 4) {
        s32 left = 0;
        s32 right = 0;
        s32 leftOutput = 0;
        s32 rightOutput = 0;

        s32 ch0 = SIM_Audio_RunChannel(512, 0);
        s32 ch1 = SIM_Audio_RunChannel(512, 1);
        s32 ch2 = SIM_Audio_RunChannel(512, 2);
        s32 ch3 = SIM_Audio_RunChannel(512, 3);

        // hack for now
        PanOutput(ch0, &left, &right, 0);
        PanOutput(ch1, &left, &right, 1);
        PanOutput(ch2, &left, &right, 2);
        PanOutput(ch3, &left, &right, 3);
        //left += ch0;
        //left += ch1;
        //left += ch2;
        //left += ch3;

        // pan output

        for(int chNo = 4; chNo < 16; chNo++) {
            s32 channel = SIM_Audio_RunChannel(512, chNo);
            PanOutput(channel, &left, &right, chNo);
        }

        right = left;
        s_blipTimer += 512;

        if(left != 0) {
            blip_add_delta(s_BlipLeft, s_blipTimer, left - s_outputLastLeftSample);
        }
        if(right != 0) {
            blip_add_delta(s_BlipRight, s_blipTimer, right - s_outputLastRightSample);
        }

        s_outputLastLeftSample = left;
        s_outputLastRightSample = right;

        if(s_blipTimer >= 512 * 128) {
            blip_end_frame(s_BlipLeft, s_blipTimer);
            blip_end_frame(s_BlipRight, s_blipTimer);
            s_blipTimer = 0;
        }
    }

    int avail = blip_samples_avail(s_BlipLeft);
    if(avail > len / 4) {
        avail = len / 4;
    }
    s16 * tempbuf = (s16*)stream;
    blip_read_samples(s_BlipLeft, tempbuf, avail, TRUE);
    blip_read_samples(s_BlipRight, tempbuf+1, avail, TRUE);
    #ifdef SDK_TRACY_ENABLE
    TracyCZoneEnd(ctx);
    #endif
}

#endif /* VITAPOKE_SIM_AUDIO_DEVICE */

void SIM_Audio_StartChannel(int chNo)
{
    if(((s_SIM_sndcnt[chNo]>>29)&0x3) == 3) {
        s_SIM_internalSoundPos[chNo] = -1;
    } else {
        s_SIM_internalSoundPos[chNo] = -3;
    }
    s_SIM_internalSoundSample[chNo] = 0;
    s_SIM_internalSoundTimer[chNo] = s_SIM_sndtmr[chNo];
    s_SIM_internalNoiseVal[chNo] = 0x7FFF;
}

s32 SIM_Audio_RunChannel(u32 cycles, int chNo)
{
    if(!(s_SIM_sndcnt[chNo] & (1<<31))) {
        return 0;
    }

    int type = (s_SIM_sndcnt[chNo] >> 29) & 0x3;
    if(type == 3) {
        if(chNo >= 14) {
            type = 4;
        } else if (chNo >= 8) {
            type = 3;
        } else {
            // these channels cant do psg or noise
            return 0;
        }
    }

    if((type < 3) && 
       (s_SIM_sndlen[chNo] + s_SIM_sndpnt[chNo] < 16)) {
        return 0;
    }

    s_SIM_internalSoundTimer[chNo] += cycles;

    while(s_SIM_internalSoundTimer[chNo] >> 16) {
        s_SIM_internalSoundTimer[chNo] = s_SIM_sndtmr[chNo] + (s_SIM_internalSoundTimer[chNo] - 0x10000);

        switch(type) {
            case 0: SIM_Audio_NextSamplePCM8(chNo); break;
            case 1: SIM_Audio_NextSamplePCM16(chNo); break;
            case 2: SIM_Audio_NextSampleADPCM(chNo); break;
            case 3: SIM_Audio_NextSamplePSG(chNo); break;
            case 4: SIM_Audio_NextSampleNoise(chNo); break;
        }
    }

    s32 ret = s_SIM_internalSoundSample[chNo];
    int volumeShift = (s_SIM_sndcnt[chNo] & (0x3 << 8)) >> 8;
    int volume = s_SIM_sndcnt[chNo] & 0b1111111;

    //ret <<= volumeShift;
    ret = ret >> volumeShift;
    //ret *= volume;
    //ret = ret / 128;
    // Signed 16-bit samples times volume <=127 fit s32; C division preserves truncation toward zero.
    ret = (s16)((ret * volume) / 128);

    return ret;
}

void SIM_Audio_NextSamplePCM8(int chNo)
{
    s_SIM_internalSoundPos[chNo]++;
    if(s_SIM_internalSoundPos[chNo] < 0) {
        return;
    }
    
    if(s_SIM_internalSoundPos[chNo] >= s_SIM_sndpnt[chNo] + s_SIM_sndlen[chNo]) {
        u32 repeat = (s_SIM_sndcnt[chNo] >> 27) & 0x3;
        if(repeat & 1) {
            s_SIM_internalSoundPos[chNo] = s_SIM_sndpnt[chNo];
        } else if(repeat & 2) {
            s_SIM_internalSoundSample[chNo] = 0;
            s_SIM_sndcnt[chNo] &= ~(1<<31);
            return;
        }
    }
    s8 * ptr = (s8*)s_SIM_sndsad[chNo];
    if(ptr) {
        s8 val = *(ptr+s_SIM_internalSoundPos[chNo]);
        s_SIM_internalSoundSample[chNo] = val << 8;
    }
}

void SIM_Audio_NextSamplePCM16(int chNo)
{
    s_SIM_internalSoundPos[chNo]++;
    if(s_SIM_internalSoundPos[chNo] < 0) {
        return;
    }

    if((s_SIM_internalSoundPos[chNo]<<1) >= s_SIM_sndpnt[chNo] + s_SIM_sndlen[chNo]) {
        u32 repeat = (s_SIM_sndcnt[chNo] >> 27) & 0x3;
        if(repeat & 1) {
            s_SIM_internalSoundPos[chNo] = s_SIM_sndpnt[chNo]>>1;
        } else if(repeat & 2) {
            s_SIM_internalSoundSample[chNo] = 0;
            s_SIM_sndcnt[chNo] &= ~(1<<31);
            return;
        }
    }
    s16 * ptr = (s16*)s_SIM_sndsad[chNo];
    if(ptr) {
        s16 val = *(ptr+s_SIM_internalSoundPos[chNo]);
        s_SIM_internalSoundSample[chNo] = val;
    }
}

void SIM_Audio_NextSampleADPCM(int chNo)
{
    s_SIM_internalSoundPos[chNo]++;
    if(s_SIM_internalSoundPos[chNo] < 8) {
        if(s_SIM_internalSoundPos[chNo] == 0) {
            //setup adpcm
            u32 header = *(u32*)(s_SIM_sndsad[chNo]);
            s_SIM_internalADPCMVal[chNo] = (s32)(s16)(header & 0xFFFF);
            s_SIM_internalADPCMIndex[chNo] = (header >> 16) & 0x7F;
            s_SIM_internalNextADPCMByte[chNo] = 4;
            if(s_SIM_internalADPCMIndex[chNo] > 88) {
                s_SIM_internalADPCMIndex[chNo] = 88;
            }

            s_SIM_internalADPCMValLoop[chNo] = s_SIM_internalADPCMVal[chNo];
            s_SIM_internalADPCMIndexLoop[chNo] = s_SIM_internalADPCMIndex[chNo];
        }

        return;
    }

    if((s_SIM_internalSoundPos[chNo] >> 1) >= (s_SIM_sndpnt[chNo] + s_SIM_sndlen[chNo])) {
        u32 repeat = (s_SIM_sndcnt[chNo]>>27) & 0x3;
        if(repeat & 1) {
            s_SIM_internalSoundPos[chNo] = s_SIM_sndpnt[chNo]<<1;
            s_SIM_internalNextADPCMByte[chNo] = s_SIM_sndpnt[chNo];
            s_SIM_internalADPCMVal[chNo] = s_SIM_internalADPCMValLoop[chNo];
            s_SIM_internalADPCMIndex[chNo] = s_SIM_internalADPCMIndexLoop[chNo];
            u8 * ptr = (u8*)s_SIM_sndsad[chNo];
            s_SIM_internalADPCMCurByte[chNo] = GetNextADPCMByte(chNo);
            //s_SIM_internalADPCMCurByte[chNo] = ptr[s_SIM_internalSoundPos[chNo]>>1];
        } else if (repeat & 2) {
            s_SIM_internalSoundSample[chNo] = 0;
            s_SIM_sndcnt[chNo] &= ~(1<<31);
            return;
        }
    } else {
        if(!(s_SIM_internalSoundPos[chNo] & 0x1)) {
            //u8 * ptr = (u8*)s_SIM_sndsad[chNo];
            //s_SIM_internalADPCMCurByte[chNo] = ptr[s_SIM_internalSoundPos[chNo]>>1];
            s_SIM_internalADPCMCurByte[chNo] = GetNextADPCMByte(chNo);
        } else {
            s_SIM_internalADPCMCurByte[chNo] >>= 4;
        }

        u16 val = s_ADPCMTable[s_SIM_internalADPCMIndex[chNo]];
        u16 diff = val >> 3;
        if (s_SIM_internalADPCMCurByte[chNo] & 0x1) diff += (val >> 2);
        if (s_SIM_internalADPCMCurByte[chNo] & 0x2) diff += (val >> 1);
        if (s_SIM_internalADPCMCurByte[chNo] & 0x4) diff += val;

        if (s_SIM_internalADPCMCurByte[chNo] & 0x8)
        {
            s_SIM_internalADPCMVal[chNo] -= diff;
            if (s_SIM_internalADPCMVal[chNo] < -0x7FFF) s_SIM_internalADPCMVal[chNo] = -0x7FFF;
        }
        else
        {
            s_SIM_internalADPCMVal[chNo] += diff;
            if (s_SIM_internalADPCMVal[chNo] > 0x7FFF) s_SIM_internalADPCMVal[chNo] = 0x7FFF;
        }

        s_SIM_internalADPCMIndex[chNo] += s_ADPCMIndexTable[s_SIM_internalADPCMCurByte[chNo] & 0x7];
        if      (s_SIM_internalADPCMIndex[chNo] < 0)  s_SIM_internalADPCMIndex[chNo] = 0;
        else if (s_SIM_internalADPCMIndex[chNo] > 88) s_SIM_internalADPCMIndex[chNo] = 88;

        if (s_SIM_internalSoundPos[chNo] == (s_SIM_sndpnt[chNo]<<1))
        {
            s_SIM_internalADPCMValLoop[chNo] = s_SIM_internalADPCMVal[chNo];
            s_SIM_internalADPCMIndexLoop[chNo] = s_SIM_internalADPCMIndex[chNo];
        }
    }

    s_SIM_internalSoundSample[chNo] = s_SIM_internalADPCMVal[chNo];
}

void SIM_Audio_NextSamplePSG(int chNo)
{
    s_SIM_internalSoundPos[chNo]++;
    s_SIM_internalSoundSample[chNo] = s_PSGTable[(s_SIM_sndcnt[chNo] >> 24) & 0x7][s_SIM_internalSoundPos[chNo] & 0x7];
}

void SIM_Audio_NextSampleNoise(int chNo)
{
    if(s_SIM_internalNoiseVal[chNo] & 0x1) {
        s_SIM_internalNoiseVal[chNo] = (s_SIM_internalNoiseVal[chNo] >> 1) ^ 0x6000;
        s_SIM_internalSoundSample[chNo] = -0x7FFF;
    } else {
        s_SIM_internalNoiseVal[chNo] >>= 1;
        s_SIM_internalSoundSample[chNo] = 0x7FFF;
    }
}

static u8 GetNextADPCMByte(int chNo)
{
    u8 ret = s_SIM_sndsad[chNo][s_SIM_internalNextADPCMByte[chNo]];
    s_SIM_internalNextADPCMByte[chNo]++;

    return ret;
}

static void PanOutput(s32 in, s32 * left, s32 * right, int chNo)
{
    int pan = (s_SIM_sndcnt[chNo] & (0b1111111 << 16)) >> 16;
    *left += ((s64)in * (128-pan)) >> 10;
    *right += ((s64)in * pan) >> 10;
}
extern "C" u32 VitaNativeAudioStateHash(void){u32 hash=2166136261u;for(int i=0;i<16;i++){u32 values[]={s_SIM_sndcnt[i],s_SIM_sndtmr[i],s_SIM_sndpnt[i],s_SIM_sndlen[i],(u32)s_SIM_internalSoundTimer[i],(u32)s_SIM_internalSoundPos[i],(u32)s_SIM_internalSoundSample[i],(u32)s_SIM_internalADPCMValLoop[i],(u32)s_SIM_internalADPCMIndexLoop[i],(u32)s_SIM_internalADPCMVal[i],(u32)s_SIM_internalADPCMIndex[i],s_SIM_internalADPCMCurByte[i],(u32)s_SIM_internalNextADPCMByte[i],s_SIM_internalNoiseVal[i]};for(unsigned j=0;j<sizeof(values)/sizeof(values[0]);j++)hash=(hash^values[j])*16777619u;}return hash;}

/* ---- Silent (muted) channel advance -----------------------------------------
 * The native PSP app produces no speaker output. When VitaNativeSoundSilent is
 * nonzero the pump advances each busy channel arithmetically instead of running
 * SIM_Audio_RunChannel in 512-cycle chunks: the internal sample timer, the
 * sample position and the channel enable bit (bit 31 of s_SIM_sndcnt, which
 * SND_IsChannelActive7 and SNDi_SharedWork->channelStatus report to the game)
 * evolve exactly as the chunked decoder would leave them, including the
 * one-shot stop point (the chunk in which the end sample falls is drained the
 * same way). Decoded sample values, ADPCM predictor state and the noise LFSR
 * are not maintained while silent: nothing outside this engine reads them.
 */
extern "C" {
int VitaNativeSoundSilent = 1;
static unsigned s_silentVerifyMismatches, s_silentVerifyChecks, s_silentVerifyStops;

static inline u32 SilentSamplesFor(u32 timer, u32 cycles, u32 period)
{
    u32 t = timer + cycles;
    return t < 0x10000u ? 0u : (t - 0x10000u) / period + 1u;
}

void SIM_Audio_AdvanceChannelSilent(u32 cycles, u32 chunk, int chNo)
{
    u32 cnt = s_SIM_sndcnt[chNo];
    if(!(cnt & (1u << 31))) {
        return;
    }
    int type = (cnt >> 29) & 0x3;
    if(type == 3) {
        if(chNo >= 14) type = 4;
        else if(chNo >= 8) type = 3;
        else return;
    }
    u32 pnt = s_SIM_sndpnt[chNo], len = s_SIM_sndlen[chNo];
    if(type < 3 && pnt + len < 16) {
        return;
    }
    u32 period = 0x10000u - s_SIM_sndtmr[chNo];
    u32 timer = (u32)s_SIM_internalSoundTimer[chNo];
    s32 pos = s_SIM_internalSoundPos[chNo];
    u32 n = SilentSamplesFor(timer, cycles, period);

    if(type >= 3) {
        /* PSG advances its phase counter; the noise LFSR is output-only. */
        if(type == 3) pos += (s32)n;
        s_SIM_internalSoundPos[chNo] = pos;
        s_SIM_internalSoundTimer[chNo] = (s32)(timer + cycles - n * period);
        return;
    }

    s32 start, end;
    switch(type) {
        case 0:  start = (s32)pnt;      end = (s32)(pnt + len);            break;
        case 1:  start = (s32)(pnt >> 1); end = (s32)((pnt + len + 1) >> 1); break;
        default: start = (s32)(pnt << 1); end = (s32)((pnt + len) << 1);     break;
    }
    u32 repeat = (cnt >> 27) & 0x3;
    if(repeat & 1) {
        s32 next = pos + (s32)n;
        if(next >= end) {
            s32 loopLen = end - start;
            next = loopLen > 0 ? start + (s32)((u32)(next - end) % (u32)loopLen) : start;
        }
        s_SIM_internalSoundPos[chNo] = next;
        s_SIM_internalSoundTimer[chNo] = (s32)(timer + cycles - n * period);
        return;
    }
    if(!(repeat & 2)) {
        /* Manual mode never ends: the decoder keeps reading past the loop end. */
        s_SIM_internalSoundPos[chNo] = pos + (s32)n;
        s_SIM_internalSoundTimer[chNo] = (s32)(timer + cycles - n * period);
        return;
    }
    /* One-shot: stops at the first increment that reaches the end. */
    s32 untilEnd = end - pos;
    if(untilEnd < 1) untilEnd = 1;
    if(n < (u32)untilEnd) {
        s_SIM_internalSoundPos[chNo] = pos + (s32)n;
        s_SIM_internalSoundTimer[chNo] = (s32)(timer + cycles - n * period);
        return;
    }
    /* Cycles needed to reach the end sample, rounded up to the chunk in which
     * it happens; that chunk is drained completely before the channel is seen
     * inactive, so later samples in it still advance the position. */
    u32 need = 0x10000u + ((u32)untilEnd - 1u) * period - timer;
    u32 fed = ((need + chunk - 1u) / chunk) * chunk;
    if(fed > cycles) fed = cycles;
    u32 m = SilentSamplesFor(timer, fed, period);
    s_SIM_internalSoundPos[chNo] = pos + (s32)m;
    s_SIM_internalSoundTimer[chNo] = (s32)(timer + fed - m * period);
    s_SIM_internalSoundSample[chNo] = 0;
    s_SIM_sndcnt[chNo] = cnt & ~(1u << 31);
}

/* Verification: run the silent advance on a copy of the state, then the
 * original chunked decoder, and compare enable bit, timer and position. The
 * decoder's result is what remains in effect. */
void SIM_Audio_VerifySilentAdvance(u32 cycles, u32 chunk, int chNo)
{
    u32 cnt = s_SIM_sndcnt[chNo]; s32 timer = s_SIM_internalSoundTimer[chNo];
    s32 pos = s_SIM_internalSoundPos[chNo]; s32 sample = s_SIM_internalSoundSample[chNo];
    SIM_Audio_AdvanceChannelSilent(cycles, chunk, chNo);
    u32 cnt2 = s_SIM_sndcnt[chNo]; s32 timer2 = s_SIM_internalSoundTimer[chNo]; s32 pos2 = s_SIM_internalSoundPos[chNo];
    s_SIM_sndcnt[chNo] = cnt; s_SIM_internalSoundTimer[chNo] = timer;
    s_SIM_internalSoundPos[chNo] = pos; s_SIM_internalSoundSample[chNo] = sample;
    for(u32 left = cycles; left; ) {
        u32 n = left > chunk ? chunk : left;
        SIM_Audio_RunChannel(n, chNo);
        left -= n;
    }
    s_silentVerifyChecks++;
    if((cnt & (1u << 31)) && !(s_SIM_sndcnt[chNo] & (1u << 31))) s_silentVerifyStops++;
    if(cnt2 != s_SIM_sndcnt[chNo] || timer2 != s_SIM_internalSoundTimer[chNo] || pos2 != s_SIM_internalSoundPos[chNo]) {
        s_silentVerifyMismatches++;
        if(s_silentVerifyMismatches <= 20)
            printf("[AUDIO-VERIFY] ch=%d cnt=%08lx/%08lx timer=%ld/%ld pos=%ld/%ld start cnt=%08lx timer=%ld pos=%ld tmr=%u pnt=%u len=%lu\n",
                   chNo, (unsigned long)cnt2, (unsigned long)s_SIM_sndcnt[chNo], (long)timer2, (long)s_SIM_internalSoundTimer[chNo],
                   (long)pos2, (long)s_SIM_internalSoundPos[chNo], (unsigned long)cnt, (long)timer, (long)pos,
                   s_SIM_sndtmr[chNo], s_SIM_sndpnt[chNo], (unsigned long)s_SIM_sndlen[chNo]);
    }
}
unsigned VitaNativeSoundSilentVerifyChecks(void){return s_silentVerifyChecks;}
unsigned VitaNativeSoundSilentVerifyMismatches(void){return s_silentVerifyMismatches;}
unsigned VitaNativeSoundSilentVerifyStops(void){return s_silentVerifyStops;}
}

/* ---- Cheap block mixer for the PSP native port -------------------------------
 * Replaces the per-512-cycle, per-sample, all-16-channel round trip through
 * SIM_Audio_RunChannel with one tight loop per active channel per block:
 *   - one output sample per VITAPOKE_AUDIO_STEP_CYCLES ARM7 cycles (DS-native rate
 *     class, ~32 kHz) instead of one per 512 cycles (~65 kHz): half the work;
 *   - the sample type is dispatched once per channel per block, not per sample,
 *     via a template, so the decoder is inlined straight into the loop;
 *   - volume, volume shift and pan collapse into two integer gains computed
 *     once per channel per block;
 *   - inactive channels, muted channels and channels the DS would ignore are
 *     skipped before any state is touched;
 *   - all per-channel engine state is held in locals for the whole block and
 *     written back once.
 * Output scaling is bit-identical in form to the reference PanOutput path:
 *   out += ((sample >> volumeShift) * volume * (128 - pan)) >> 17
 * (reference: ret = (sample>>shift)*vol/128, then left += ret*(128-pan) >> 10).
 */
/* The DS channel timers are clocked at 16.756991 MHz (ARM7 clock / 2), which is
 * the rate SIM_Audio_RunChannel's `cycles` argument counts in - see
 * INTERNAL_SAMPLE_RATE above. One output sample every 524 ticks is therefore
 * 16756991/524 = 31979.0 Hz, played out through a 32000 Hz sceAudio SRC
 * channel: 0.066% sharp, about a hundredth of a semitone.
 * (The reference SDL mixer used 512 ticks = 32728 Hz.) */
#ifndef VITAPOKE_AUDIO_QUIET
#define VITAPOKE_AUDIO_QUIET 32
#endif
/* Cheap mode (the default). VITAPOKE_AUDIO_MONO=1 drops per-channel panning: one
 * multiply and one accumulator per sample instead of two, and the block is
 * duplicated to both speakers at the end. VITAPOKE_AUDIO_MAX_VOICES caps how many
 * channels are decoded per block - the loudest N survive, the rest get the
 * arithmetic advance. Decoding is the mixer's dominant cost (the soundtrack is
 * all ADPCM), so the cap is the single biggest lever there is.
 * -DVITAPOKE_AUDIO_MONO=0 -DVITAPOKE_AUDIO_MAX_VOICES=16 restores full quality. */
#ifndef VITAPOKE_AUDIO_MONO
#define VITAPOKE_AUDIO_MONO 1
#endif
#ifndef VITAPOKE_AUDIO_MAX_VOICES
#define VITAPOKE_AUDIO_MAX_VOICES 6
#endif
/* Masking threshold, in output units, below which a channel is advanced but
 * not decoded. Set once per block by the backend from the running peak of the
 * mix (see VITAPOKE_AUDIO_QUIET_SHIFT in backend.c), so a loud passage hides more
 * and a quiet one hides almost nothing. */
extern "C" int VitaNativeAudioQuietLevel;
int VitaNativeAudioQuietLevel = VITAPOKE_AUDIO_QUIET;
#ifndef VITAPOKE_AUDIO_STEP_CYCLES
#define VITAPOKE_AUDIO_STEP_CYCLES 524
#endif

static unsigned s_mixActiveMask;

/* The whole Platinum soundtrack is IMA-ADPCM: a measured overworld run had
 * 110,938,330 of 110,959,313 sample decodes on type 2 and none at all on PSG
 * or noise. So the decoder's inner arithmetic is the mixer's hot path, and it
 * collapses into two flat lookups indexed by (predictor index, 4-bit code):
 * the signed step to add, and the next predictor index (already clamped).
 * 89*16 entries: 5.7 KB of s32 plus 1.4 KB of u8, built once. */
static s32 s_adpcmDelta[89 * 16];
static u8 s_adpcmNextIndex[89 * 16];
static int s_adpcmTablesReady;

static void BuildADPCMTables(void)
{
    for (int idx = 0; idx < 89; idx++) {
        u32 val = s_ADPCMTable[idx];
        for (int c = 0; c < 16; c++) {
            u32 diff = val >> 3;
            if (c & 0x1) diff += val >> 2;
            if (c & 0x2) diff += val >> 1;
            if (c & 0x4) diff += val;
            s_adpcmDelta[idx * 16 + c] = (c & 0x8) ? -(s32)diff : (s32)diff;
            int next = idx + s_ADPCMIndexTable[c & 0x7];
            if (next < 0) next = 0; else if (next > 88) next = 88;
            s_adpcmNextIndex[idx * 16 + c] = (u8)next;
        }
    }
    s_adpcmTablesReady = 1;
}

#ifdef VITAPOKE_AUDIO_STATS
static unsigned s_mixDecodes, s_mixSamples, s_mixChanBlocks, s_mixLiveMax;
static unsigned s_decByType[5], s_blkByType[5], s_quietBlk, s_quietDec, s_centreBlk;
extern "C" void VitaNativeAudioMixStats2(unsigned*d,unsigned*b,unsigned*q){for(int i=0;i<5;i++){d[i]=s_decByType[i];b[i]=s_blkByType[i];}q[0]=s_quietBlk;q[1]=s_quietDec;q[2]=s_centreBlk;}
extern "C" void VitaNativeAudioMixStats(unsigned*d,unsigned*s,unsigned*c,unsigned*m){*d=s_mixDecodes;*s=s_mixSamples;*c=s_mixChanBlocks;*m=s_mixLiveMax;}
#endif

template <int TYPE>
static void MixChannelBlock(s32 *__restrict outL, s32 *__restrict outR,
                            unsigned n, u32 step, int ch, int gl, int gr, int gshift)
{
    const u32 cnt0 = s_SIM_sndcnt[ch];
    const u32 pnt = s_SIM_sndpnt[ch];
    const u32 len = s_SIM_sndlen[ch];
    const u32 repeat = (cnt0 >> 27) & 0x3;
    const s32 period = (s32)(0x10000u - s_SIM_sndtmr[ch]);
    s32 timer = s_SIM_internalSoundTimer[ch];
    s32 pos = s_SIM_internalSoundPos[ch];
    s32 sample = s_SIM_internalSoundSample[ch];
    u8 *const base = s_SIM_sndsad[ch];

    /* PCM end / loop points, in the same units the reference decoders use. */
    s32 pcmEnd = 0, pcmLoop = 0;
    if (TYPE == 0) { pcmEnd = (s32)(pnt + len); pcmLoop = (s32)pnt; }
    if (TYPE == 1) { pcmEnd = (s32)(pnt + len); pcmLoop = (s32)(pnt >> 1); }
    if (TYPE == 2) { pcmEnd = (s32)(pnt + len); pcmLoop = (s32)(pnt << 1); }

    s32 aVal = 0, aIdx = 0, aLoopVal = 0, aLoopIdx = 0, aNext = 0;
    u8 aCur = 0;
    if (TYPE == 2) {
        aVal = s_SIM_internalADPCMVal[ch];
        aIdx = s_SIM_internalADPCMIndex[ch];
        aLoopVal = s_SIM_internalADPCMValLoop[ch];
        aLoopIdx = s_SIM_internalADPCMIndexLoop[ch];
        aNext = s_SIM_internalNextADPCMByte[ch];
        aCur = s_SIM_internalADPCMCurByte[ch];
    }
    const s16 *psgRow = 0;
    if (TYPE == 3) psgRow = s_PSGTable[(cnt0 >> 24) & 0x7];
    u16 noise = 0;
    if (TYPE == 4) noise = s_SIM_internalNoiseVal[ch];

    int stopped = 0;
    unsigned i = 0;
    for (; i < n; i++) {
        timer += (s32)step;
        while (timer >= 0x10000) {
            timer -= period;
#ifdef VITAPOKE_AUDIO_STATS
            s_mixDecodes++; s_decByType[TYPE]++;
#endif
            if (TYPE == 0) {                                   /* PCM8 */
                pos++;
                if (pos >= 0) {
                    if (pos >= pcmEnd) {
                        if (repeat & 1) pos = pcmLoop;
                        else if (repeat & 2) { sample = 0; stopped = 1; break; }
                    }
                    if (base) sample = (s32)((s8)base[pos]) << 8;
                }
            } else if (TYPE == 1) {                            /* PCM16 */
                pos++;
                if (pos >= 0) {
                    if ((pos << 1) >= pcmEnd) {
                        if (repeat & 1) pos = pcmLoop;
                        else if (repeat & 2) { sample = 0; stopped = 1; break; }
                    }
                    if (base) sample = (s32)*((const s16 *)base + pos);
                }
            } else if (TYPE == 2) {                            /* IMA-ADPCM */
                pos++;
                if (pos < 8) {
                    if (pos == 0) {
                        u32 header = *(const u32 *)base;
                        aVal = (s32)(s16)(header & 0xFFFF);
                        aIdx = (s32)((header >> 16) & 0x7F);
                        if (aIdx > 88) aIdx = 88;
                        aNext = 4;
                        aLoopVal = aVal;
                        aLoopIdx = aIdx;
                    }
                } else {
                    if ((pos >> 1) >= pcmEnd) {
                        if (repeat & 1) {
                            pos = pcmLoop;
                            aNext = (s32)pnt;
                            aVal = aLoopVal;
                            aIdx = aLoopIdx;
                            aCur = base[aNext++];
                        } else if (repeat & 2) { sample = 0; stopped = 1; break; }
                    } else {
                        if (!(pos & 1)) aCur = base[aNext++];
                        else aCur = (u8)(aCur >> 4);
                    }
                    {
                        const unsigned k = (unsigned)aIdx * 16u + (aCur & 0xFu);
                        aVal += s_adpcmDelta[k];
                        if (aVal > 0x7FFF) aVal = 0x7FFF;
                        else if (aVal < -0x7FFF) aVal = -0x7FFF;
                        aIdx = (s32)s_adpcmNextIndex[k];
                        if (pos == pcmLoop) { aLoopVal = aVal; aLoopIdx = aIdx; }
                    }
                    sample = aVal;
                }
            } else if (TYPE == 3) {                            /* PSG */
                pos++;
                sample = psgRow[pos & 0x7];
            } else {                                           /* noise */
                if (noise & 1) { noise = (u16)((noise >> 1) ^ 0x6000); sample = -0x7FFF; }
                else { noise = (u16)(noise >> 1); sample = 0x7FFF; }
            }
        }
#if VITAPOKE_AUDIO_MONO
        outL[i] += (sample * gl) >> gshift;
#else
        {
            /* gl/gr already carry volume * pan and gshift carries the channel
             * volume divider, so this is two multiplies and two shifts. */
            outL[i] += (sample * gl) >> gshift;
            outR[i] += (sample * gr) >> gshift;
        }
#endif
        if (stopped) { i++; break; }
    }

    s_SIM_internalSoundTimer[ch] = timer;
    s_SIM_internalSoundPos[ch] = pos;
    s_SIM_internalSoundSample[ch] = sample;
    if (TYPE == 2) {
        s_SIM_internalADPCMVal[ch] = aVal;
        s_SIM_internalADPCMIndex[ch] = aIdx;
        s_SIM_internalADPCMValLoop[ch] = aLoopVal;
        s_SIM_internalADPCMIndexLoop[ch] = aLoopIdx;
        s_SIM_internalNextADPCMByte[ch] = aNext;
        s_SIM_internalADPCMCurByte[ch] = aCur;
    }
    if (TYPE == 4) s_SIM_internalNoiseVal[ch] = noise;
    if (stopped) s_SIM_sndcnt[ch] &= ~(1u << 31);
}

extern "C" {

/* Mix `n` samples (one every VITAPOKE_AUDIO_STEP_CYCLES ARM7 cycles) of every busy
 * channel into the two accumulators, which the caller has already cleared. */
void VitaNativeAudioMixBlock(s32 *outL, s32 *outR, unsigned n)
{
    const u32 step = VITAPOKE_AUDIO_STEP_CYCLES;
    const u32 cycles = step * n;
    int chan[16], type_[16], gl_[16], gr_[16], gshift_[16], level[16];
    int nc = 0;
    if (!s_adpcmTablesReady) BuildADPCMTables();

    /* Pass 1: who could be heard, and how loudly at most. Touches no state. */
    for (int ch = 0; ch < 16; ch++) {
        const u32 cnt = s_SIM_sndcnt[ch];
        if (!(cnt & (1u << 31))) continue;
        int type = (int)((cnt >> 29) & 0x3);
        if (type == 3) {
            if (ch >= 14) type = 4;
            else if (ch >= 8) type = 3;
            else continue;            /* these channels cannot do PSG or noise */
        }
        if (type < 3 && s_SIM_sndlen[ch] + s_SIM_sndpnt[ch] < 16) continue;
        s_mixActiveMask |= 1u << ch;
        const int vol = (int)(cnt & 0x7F);
        const int pan = (int)((cnt >> 16) & 0x7F);
        const int gshift = 17 + (int)((cnt >> 8) & 0x3);
#if VITAPOKE_AUDIO_MONO
        const int gl = vol << 7, gr = gl;
#else
        const int gl = vol * (128 - pan), gr = vol * pan;
#endif
        chan[nc] = ch; type_[nc] = type; gl_[nc] = gl; gr_[nc] = gr;
        gshift_[nc] = gshift;
        /* Loudest output unit this channel could possibly produce this block. */
        level[nc] = (gl > gr ? gl : gr) >> (gshift - 15);
        nc++;
    }

    /* Pass 2: the masking floor, raised if more voices are live than the cap. */
    int floorLevel = VitaNativeAudioQuietLevel;
    if (nc > VITAPOKE_AUDIO_MAX_VOICES) {
        /* Level of the (MAX_VOICES)th loudest: selection over at most 16. */
        int sorted[16];
        for (int i = 0; i < nc; i++) sorted[i] = level[i];
        for (int i = 0; i < VITAPOKE_AUDIO_MAX_VOICES; i++) {
            int best = i;
            for (int j = i + 1; j < nc; j++) if (sorted[j] > sorted[best]) best = j;
            int t = sorted[i]; sorted[i] = sorted[best]; sorted[best] = t;
        }
        int cut = sorted[VITAPOKE_AUDIO_MAX_VOICES - 1];
        if (cut > floorLevel) floorLevel = cut;
    }

    /* Pass 3: decode the survivors, advance the rest arithmetically. */
    int mixed = 0;
    for (int i = 0; i < nc; i++) {
        const int ch = chan[i];
        if (level[i] < floorLevel || mixed >= VITAPOKE_AUDIO_MAX_VOICES) {
            SIM_Audio_AdvanceChannelSilent(cycles, step, ch);
            continue;
        }
        mixed++;
        switch (type_[i]) {
            case 0: MixChannelBlock<0>(outL, outR, n, step, ch, gl_[i], gr_[i], gshift_[i]); break;
            case 1: MixChannelBlock<1>(outL, outR, n, step, ch, gl_[i], gr_[i], gshift_[i]); break;
            case 2: MixChannelBlock<2>(outL, outR, n, step, ch, gl_[i], gr_[i], gshift_[i]); break;
            case 3: MixChannelBlock<3>(outL, outR, n, step, ch, gl_[i], gr_[i], gshift_[i]); break;
            default: MixChannelBlock<4>(outL, outR, n, step, ch, gl_[i], gr_[i], gshift_[i]); break;
        }
    }
}

unsigned VitaNativeAudioMixActiveMask(void) { return s_mixActiveMask; }

} /* extern "C" */
