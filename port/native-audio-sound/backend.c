#include <nitro.h>
#include <nitro/snd/common/work.h>
#include <simulator/sim_audio.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define SND_MSG_REQUEST_COMMAND_PROC 0
#define SND_COMMAND_NUM 256
#ifndef VITAPOKE_AUDIO_MONO
#define VITAPOKE_AUDIO_MONO 1
#endif
#ifndef VITAPOKE_AUDIO_GAIN_SHIFT
#if VITAPOKE_AUDIO_MONO
#define VITAPOKE_AUDIO_GAIN_SHIFT 1   /* mono puts the whole sum on both speakers */
#else
#define VITAPOKE_AUDIO_GAIN_SHIFT 2
#endif
#endif
/* Output gain. The reference DS mixer this decoder came from scales each
   channel by volume/128 then pan/1024, which peaks 16 dB below full scale on
   real Platinum content (measured: -16.4 dBFS peak, -35.2 dBFS RMS over the
   intro, title and overworld). Two bits of gain put the peak near -4 dBFS
   with the hard clamp below as the only limiter. */
/* Masking: a channel that cannot reach peak>>SHIFT output units is skipped.
   4 = 24 dB below the running peak of the mix. */
#ifndef VITAPOKE_AUDIO_QUIET_SHIFT
#define VITAPOKE_AUDIO_QUIET_SHIFT 4
#endif
#ifndef VITAPOKE_AUDIO_QUIET_MIN
#define VITAPOKE_AUDIO_QUIET_MIN 32
#endif
#ifndef VITAPOKE_AUDIO_QUIET_MAX
#define VITAPOKE_AUDIO_QUIET_MAX 256
#endif
#define UNPACK_COMMAND(v,shift,bits) (((v)>>(shift))&((1ull<<(bits))-1))
static PXIFifoCallback callback;
static BOOL initialized;
static unsigned lists,commands,pumps,activeSeen,tickPeak;static u32 sampleHash=2166136261u;static u64 samples,energy,pumpMicros,clockRemainder,cyclePending;
extern void SNDi_SetPlayerParam7(int,u32,u32,int);
extern void SNDi_SetTrackParam7(int,u32,u32,u32,int);
extern void SNDi_SetSurroundDecay7(int);
extern void SND_LockChannel7(u32,u32);extern void SND_UnlockChannel7(u32,u32);extern void SND_StopUnlockedChannel7(u32,u32);
extern void SND_SetMasterPan7(int);

/* ---- speaker output ---------------------------------------------------------
 * VitaNativeSoundSilent selects the cheap arithmetic channel advance -- no decode, no mix -- and is
 * what runs if the audio port could not be opened. A failure is logged and the game plays on in
 * silence rather than not starting.
 */
#define VITAPOKE_AUDIO_PUMP_CYCLES (SND_PROC_INTERVAL * 64u)     /* 174592 ARM7 cycles */
/* What one pump is worth at the output rate: 174592 cycles of a 33.51 MHz ARM7 is 5.21 ms, which at
 * 16 kHz is 83 samples. Used to size the refill correction below in units it can reason about. */
#define VITAPOKE_AUDIO_PUMP_SAMPLES \
	((unsigned)(((u64)VITAPOKE_AUDIO_OUT_FREQ * VITAPOKE_AUDIO_PUMP_CYCLES) / OS_SYSTEM_CLOCK))
#ifndef VITAPOKE_AUDIO_OUT_FREQ
#define VITAPOKE_AUDIO_OUT_FREQ    16000u
#endif
#define VITAPOKE_AUDIO_MAX_BLOCK   192                            /* > 174592/1047 */
extern void VitaNativeAudioMixBlock(s32 *outL, s32 *outR, unsigned n);
extern unsigned VitaNativeAudioMixActiveMask(void);
extern int VitaNativeAudioOutInit(void);
extern int VitaNativeAudioOutReady(void);
extern void VitaNativeAudioOutWrite(const short *interleaved, unsigned frames);
extern unsigned VitaNativeAudioOutFill(void);
extern unsigned VitaNativeAudioOutTargetFill(void);
extern unsigned VitaNativeAudioOutCapacity(void);
extern void VitaNativeAudioOutStats(unsigned *, unsigned *, unsigned *, unsigned *);
extern void VitaNativeMemLog(const char *fmt, ...);
extern void VitaNativeFatal(const char *message);
#ifdef VITAPOKE_AUDIO_WAV
extern void VitaNativeAudioDump(const short *interleaved, unsigned frames);
#endif
static int outputReady;
static u64 blockAcc;                 /* fractional output samples carried between pumps */
static s32 mixL[VITAPOKE_AUDIO_MAX_BLOCK], mixR[VITAPOKE_AUDIO_MAX_BLOCK];
static short mixOut[VITAPOKE_AUDIO_MAX_BLOCK * 2];
static unsigned mixPeak, mixClips, peakEnv;

static void Unsupported(unsigned id){printf("[AUDIO] unsupported command %u\n",id);abort();}
static void SetLocal(int p,int v,s16 n){if(p<0||p>=SND_PLAYER_NUM||v<0||v>=SND_PLAYER_VARIABLE_NUM)abort();SNDi_SharedWork->player[p].variable[v]=n;}
static void SetGlobal(int v,s16 n){if(v<0||v>=SND_GLOBAL_VARIABLE_NUM)abort();SNDi_SharedWork->globalVariable[v]=n;}
static void SetVolume(int v){if(v<0||v>127)abort();s_reg_SND_SOUNDCNT=(s_reg_SND_SOUNDCNT&~127)|v;}
static void SetOutput(SNDOutput l,SNDOutput r,SNDChannelOut a,SNDChannelOut b){s_reg_SND_SOUNDCNT=(s_reg_SND_SOUNDCNT&~0x3f00)|(l<<8)|(r<<10)|(a<<12)|(b<<13);}
static void SetChannelTimer(u32 mask,int t){for(int i=0;i<16;i++)if(mask&(1u<<i))SND_SetChannelTimer7(i,t);}
static void SetChannelVolume(u32 mask,int v,SNDChannelDataShift s){for(int i=0;i<16;i++)if(mask&(1u<<i))SND_SetChannelVolume7(i,v,s);}
static void SetChannelPan(u32 mask,int p){for(int i=0;i<16;i++)if(mask&(1u<<i))SND_SetChannelPan7(i,p);}
static void StartTimer(u32 ch,u32 capture,u32 alarm,u32 flags){if(capture||alarm)Unsupported(SND_COMMAND_START_TIMER);for(int i=0;i<16;i++)if(ch&(1u<<i))SND_StartChannel7(i);}
static void StopTimer(u32 ch,u32 capture,u32 alarm,u32 flags){if(capture||alarm)Unsupported(SND_COMMAND_STOP_TIMER);for(int i=0;i<16;i++)if(ch&(1u<<i))SND_StopChannel7(i,flags);}
static void ReadDriverInfo(SNDDriverInfo*out){memcpy(&out->work,&SNDi_Work,sizeof(SNDi_Work));memcpy(out->chCtrl,s_SIM_sndcnt,sizeof(out->chCtrl));out->workAddress=&SNDi_Work;out->lockedChannels=SND_GetLockedChannel(0);}
void VitaNativeSoundInit(void){if(initialized)return;SND_ExChannelInit();SND_SeqInit();s_reg_SND_SOUNDCNT=0x807f;initialized=TRUE;
 extern int VitaNativeSoundSilent;
 outputReady = (VitaNativeAudioOutInit() == 0);
#if defined(VITAPOKE_AUDIO_OFF)
 VitaNativeSoundSilent = 1;   /* measurement build: the pre-audio silent baseline */
#elif defined(VITAPOKE_AUDIO_WAV) || defined(VITAPOKE_AUDIO_NOOUT)
 VitaNativeSoundSilent = 0;   /* measurement build: always mix, even with no channel */
#else
 /* Sound on when there is somewhere to play it. The PSP build defaulted to off, because mixing
    cost it about five frames a second on a 333 MHz single core; the Vita mixes on one of four
    444 MHz cores while the game runs on another, so there is no reason to start silent. */
 VitaNativeSoundSilent = outputReady ? 0 : 1;
#endif
 VitaNativeMemLog("[AUDIO] init output=%s sound=%s", outputReady ? "ready" : "unavailable", VitaNativeSoundSilent ? "off" : "on");
}
void PXI_InitFifo(void){VitaNativeSoundInit();}
void PXI_SetFifoRecvCallback(int tag,PXIFifoCallback fn){if(tag!=PXI_FIFO_TAG_SOUND){printf("[AUDIO] unsupported FIFO callback %d\n",tag);abort();}callback=fn;VitaNativeSoundInit();}
BOOL PXI_IsCallbackReady(int tag,PXIProc proc){return tag==PXI_FIFO_TAG_SOUND&&initialized;}
/* The ARM7's side of the sound command FIFO. On the DS the other processor reads a linked list of
 * commands out of shared memory; here it is read in place, on the thread that sent it. The failures
 * below say what was wrong rather than aborting, because a list that does not terminate is worth
 * knowing the shape of: the SDK's pool holds at most SND_COMMAND_NUM commands, so anything longer
 * than that is a cycle.
 *
 * ------------------------------------------------------------------ 64-bit DS addresses are signed
 *
 * The word carried by this FIFO is a DS address, and on a port the SDK widens it to 64 bits:
 * snd_command.c sends `(u64)sReserveList`. A cast from a 32-bit pointer to a wider integer goes
 * through a *signed* intermediate in GCC -- the generated code is literally `asrs r1, r0, #31` -- so
 * a pointer with its top bit set arrives here as 0xffffffff_8xxxxxxx.
 *
 * On the PSP that never happened: user memory was at 0x08800000 and the top bit was always clear. On
 * the Vita every address has it set, so *every* `(u64)pointer` in this codebase is sign-extended.
 * Most of it survives because the value is cast straight back to a pointer, which truncates; what
 * does not survive is anything that range-checks or compares the 64-bit value. This is the first
 * place that did.
 *
 * So the low 32 bits are the address, as they are on the DS, and that is what this uses. Anywhere
 * else in the port that receives a DS address as 64 bits has to do the same.
 */
int PXI_SendWordByFifo(int tag,u64 data,BOOL error){
 char message[192];
 if(tag!=PXI_FIFO_TAG_SOUND||error){printf("[AUDIO] unsupported FIFO send %d\n",tag);return -1;}
 if(data==SND_MSG_REQUEST_COMMAND_PROC)return 0;
 data&=0xffffffffull;
 if(data<4096||data>UINT32_MAX||(data&3)){
  snprintf(message,sizeof message,"sound command list at %08llx is not a usable address",(unsigned long long)data);
  VitaNativeFatal(message);
 }
 const SNDCommand*p=(const SNDCommand*)(uintptr_t)data;unsigned limit=0;
 while(p){
  if(++limit>SND_COMMAND_NUM){
   const SNDCommand*q=(const SNDCommand*)(uintptr_t)data;
   snprintf(message,sizeof message,
            "sound command list does not end: over %u commands, list %u, head %p id %lu, then %p %p %p, now %p",
            (unsigned)SND_COMMAND_NUM,lists,(const void*)q,(unsigned long)q->id,(const void*)q->next,
            (const void*)(q->next?q->next->next:NULL),
            (const void*)(q->next&&q->next->next?q->next->next->next:NULL),(const void*)p);
   VitaNativeFatal(message);
  }
  SNDCommand command=*p;commands++;
 #include "consumer_switch.inc"
 p=command.next;
 }
 if(!SNDi_SharedWork){
  snprintf(message,sizeof message,"the sound engine's shared work area was never set (list %u, %u commands)",lists,commands);
  VitaNativeFatal(message);
 }
 SNDi_SharedWork->finishCommandTag++;lists++;return 0;
}
void VitaNativeSoundPump(void){
 u64 started=sceKernelGetSystemTimeWide();
#ifdef VITAPOKE_AUDIO_PROFILE
 static u64 tUpd,tSeq,tEx,tLoop,tStat;u64 t0=started,t1;
 SND_UpdateExChannel();t1=sceKernelGetSystemTimeWide();tUpd+=t1-t0;t0=t1;
 SND_SeqMain(TRUE);t1=sceKernelGetSystemTimeWide();tSeq+=t1-t0;t0=t1;
 SND_ExChannelMain(TRUE);t1=sceKernelGetSystemTimeWide();tEx+=t1-t0;t0=t1;pumps++;
#else
 SND_UpdateExChannel();SND_SeqMain(TRUE);SND_ExChannelMain(TRUE);pumps++;
#endif
 // One Nitro sound interval is 2728 DS OS ticks: 174592 ARM7 cycles.
 // Inactive channels return zero before modifying any sample-engine state.
 // Only the sequencer above can start a channel during this synchronous pump.
 unsigned active=0;
 for(unsigned ch=0;ch<16;ch++)if(s_SIM_sndcnt[ch]&(1u<<31))active|=1u<<ch;
 extern int VitaNativeSoundSilent;extern void SIM_Audio_AdvanceChannelSilent(u32,u32,int);extern void SIM_Audio_VerifySilentAdvance(u32,u32,int);
 if(VitaNativeSoundSilent){
  // Muted: no sample is decoded or mixed. Each busy channel's timer, position and
  // enable bit advance arithmetically to the values the chunked decoder would leave.
  for(unsigned pending=active;pending;){unsigned ch=__builtin_ctz(pending);pending&=pending-1;
#ifdef VITAPOKE_AUDIO_VERIFY
   SIM_Audio_VerifySilentAdvance(174592,512,ch);
#else
   SIM_Audio_AdvanceChannelSilent(174592,512,ch);
#endif
  }
  samples+=(174592/512)*16;
 }else{
  /* Real output. One block per pump: VITAPOKE_AUDIO_PUMP_CYCLES of DS time mixed at
   * one output sample per VITAPOKE_AUDIO_STEP_CYCLES (1047) ARM7 cycles, which is
   * exactly VITAPOKE_AUDIO_OUT_FREQ samples per second of DS time. The fractional
   * sample per pump is carried in blockAcc so the long-run rate is exact. */
  blockAcc += (u64)VITAPOKE_AUDIO_OUT_FREQ * VITAPOKE_AUDIO_PUMP_CYCLES;
  unsigned n = (unsigned)(blockAcc / (u64)OS_SYSTEM_CLOCK);
  blockAcc -= (u64)n * (u64)OS_SYSTEM_CLOCK;
  if(n > VITAPOKE_AUDIO_MAX_BLOCK) n = VITAPOKE_AUDIO_MAX_BLOCK;
  if(n){
   unsigned blockPeak=0;
   memset(mixL,0,n*sizeof(s32));
#if VITAPOKE_AUDIO_MONO
   /* Cheap mode: the mixer never writes mixR. One clamp, one store pair. */
   VitaNativeAudioMixBlock(mixL,mixR,n);
   for(unsigned i=0;i<n;i++){
    s32 l=mixL[i]<<VITAPOKE_AUDIO_GAIN_SHIFT;
    if(l>32767){l=32767;mixClips++;}else if(l<-32768){l=-32768;mixClips++;}
    mixOut[i*2]=mixOut[i*2+1]=(short)l;
    {unsigned a=(unsigned)(l<0?-l:l);if(a>blockPeak)blockPeak=a;}
   }
#else
   memset(mixR,0,n*sizeof(s32));
   VitaNativeAudioMixBlock(mixL,mixR,n);
   for(unsigned i=0;i<n;i++){
    s32 l=mixL[i]<<VITAPOKE_AUDIO_GAIN_SHIFT, r=mixR[i]<<VITAPOKE_AUDIO_GAIN_SHIFT;
    if(l>32767){l=32767;mixClips++;}else if(l<-32768){l=-32768;mixClips++;}
    if(r>32767){r=32767;mixClips++;}else if(r<-32768){r=-32768;mixClips++;}
    mixOut[i*2]=(short)l;mixOut[i*2+1]=(short)r;
    {unsigned a=(unsigned)(l<0?-l:l);if(a>blockPeak)blockPeak=a;}
   }
#endif
   if(blockPeak>mixPeak)mixPeak=blockPeak;energy+=blockPeak;
   /* Envelope of the mix peak: instant attack, ~1/32 per block decay (about
      170 ms to fall by half), used as the masking reference for the next block. */
   peakEnv=blockPeak>peakEnv?blockPeak:peakEnv-(peakEnv>>5);
   {extern int VitaNativeAudioQuietLevel;int q=(int)(peakEnv>>VITAPOKE_AUDIO_QUIET_SHIFT);
    if(q<VITAPOKE_AUDIO_QUIET_MIN)q=VITAPOKE_AUDIO_QUIET_MIN;else if(q>VITAPOKE_AUDIO_QUIET_MAX)q=VITAPOKE_AUDIO_QUIET_MAX;
    VitaNativeAudioQuietLevel=q;}
   if(outputReady)VitaNativeAudioOutWrite(mixOut,n);
#ifdef VITAPOKE_AUDIO_WAV
   VitaNativeAudioDump(mixOut,n);
#endif
  }
  samples+=n;
 }
#ifdef VITAPOKE_AUDIO_PROFILE
 t1=sceKernelGetSystemTimeWide();tLoop+=t1-t0;t0=t1;
#endif
 if(SNDi_SharedWork){unsigned mask=0;for(int ch=0;ch<16;ch++)if(SND_IsChannelActive7(ch))mask|=1u<<ch;activeSeen|=mask;SNDi_SharedWork->channelStatus=mask;SNDi_SharedWork->captureStatus=0;for(unsigned i=0;i<SND_PLAYER_NUM;i++)if(SNDi_SharedWork->player[i].tickCounter>tickPeak)tickPeak=SNDi_SharedWork->player[i].tickCounter;}
#ifdef VITAPOKE_AUDIO_PROFILE
 t1=sceKernelGetSystemTimeWide();tStat+=t1-t0;
 if(pumps%192==0){printf("[AUDIO-PROFILE] pumps=%u upd=%llu seq=%llu ex=%llu loop=%llu stat=%llu active=%04x\n",pumps,tUpd,tSeq,tEx,tLoop,tStat,active);
#ifdef VITAPOKE_AUDIO_VERIFY
 {extern unsigned VitaNativeSoundSilentVerifyChecks(void),VitaNativeSoundSilentVerifyMismatches(void),VitaNativeSoundSilentVerifyStops(void);printf("[AUDIO-VERIFY] checks=%u mismatches=%u stops=%u\n",VitaNativeSoundSilentVerifyChecks(),VitaNativeSoundSilentVerifyMismatches(),VitaNativeSoundSilentVerifyStops());}
#endif
 tUpd=tSeq=tEx=tLoop=tStat=0;}
#endif
 pumpMicros+=sceKernelGetSystemTimeWide()-started;
}
void VitaNativeSoundReport(void){printf("[AUDIO] lists=%u commands=%u pumps=%u channelSteps=%llu energy=%llu players=%08lx\n",lists,commands,pumps,samples,energy,SNDi_SharedWork?(unsigned long)SNDi_SharedWork->playerStatus:0);}

/* Called once per rendered frame with the microseconds of real time that the
 * frame took. Sound is paced by that clock, not by a fixed 1/30 s, so a PSP
 * running the game at 26 fps still produces samples at real-time speed and the
 * sceAudio ring neither starves nor overflows. Two guards on top of that:
 * a hard cap on pumps per call (a long load must not turn into a CPU spike),
 * and a soft correction from the measured ring fill, which is the only signal
 * that tells us the real drain rate of the hardware audio channel. */
/* What the pump actually did, for the report. The ring being empty while the sample rate is right
 * is a contradiction -- the correction below exists to refill it and should fire hardest at zero --
 * so these say which half of it is untrue: how many pumps the clock asked for, how many the
 * correction added, how many ran, and the range of ring fills the correction was deciding from.
 * A frame's fill is not the fill the report happens to sample. */
extern int VitaNativeSoundSilent;   /* sim_audio.cpp: 1 = nothing is mixed */
static unsigned advClockPumps, advExtraPumps, advRanPumps, advSilent;
static unsigned advFillMin=~0u, advFillMax;

void VitaNativeSoundAdvanceStats(unsigned*clockPumps,unsigned*extraPumps,unsigned*ranPumps,
                                 unsigned*fillMin,unsigned*fillMax,unsigned*silentCalls){
 if(clockPumps)*clockPumps=advClockPumps;
 if(extraPumps)*extraPumps=advExtraPumps;
 if(ranPumps)*ranPumps=advRanPumps;
 if(fillMin)*fillMin=advFillMin==~0u?0u:advFillMin;
 if(fillMax)*fillMax=advFillMax;
 if(silentCalls)*silentCalls=advSilent;
 advClockPumps=advExtraPumps=advRanPumps=advSilent=0;advFillMin=~0u;advFillMax=0;
}

void VitaNativeSoundAdvance(u32 microseconds){
 clockRemainder+=(u64)microseconds*OS_SYSTEM_CLOCK;cyclePending+=clockRemainder/1000000;clockRemainder%=1000000;
 advClockPumps+=(unsigned)(cyclePending/VITAPOKE_AUDIO_PUMP_CYCLES);
 if(VitaNativeSoundSilent)advSilent++;
 if(outputReady){
  /* Trim by at most one pump per frame (about +-16% of the sample rate) toward
   * the target ring fill. Bounded on purpose: the real-time clock above is the
   * authority, this only absorbs the slow drift between the DS clock constant
   * and the PSP audio clock, and it cannot run away if a host drains the
   * channel at the wrong rate. */
  unsigned fill=VitaNativeAudioOutFill(),target=VitaNativeAudioOutTargetFill();
  if(fill>target*2u||fill>(VitaNativeAudioOutCapacity()*3u)/4u)
   cyclePending=cyclePending>VITAPOKE_AUDIO_PUMP_CYCLES?cyclePending-VITAPOKE_AUDIO_PUMP_CYCLES:0;
  else if(fill<target){
   /* Proportional to the shortfall rather than one pump whatever it is. A pump is about 83 samples,
    * so a single one takes a dozen frames to make up a deficit of a thousand -- and the deficit does
    * not arrive gradually, it arrives all at once when a frame runs long.
    *
    * The ceiling was three, which was not enough to hold the target at all: hardware showed the ring
    * settling around 1100 of the 2048 it aims for and never climbing, because three pumps a frame is
    * 249 samples against the 618 a 39 ms frame consumes, so a deficit that opened in one bad frame
    * was still being paid off when the next one opened another. Sitting permanently at half the
    * target is what turned every hitch into an underrun. Twelve closes the worst deficit the ring can
    * hold in one frame. Overshooting is the cheap direction -- the trim above gives it straight back,
    * and nothing is lost but a few milliseconds of buffered sound. */
   unsigned behind=(target-fill)/VITAPOKE_AUDIO_PUMP_SAMPLES;
   if(behind<1u)behind=1u;else if(behind>12u)behind=12u;
   cyclePending+=VITAPOKE_AUDIO_PUMP_CYCLES*behind;
   advExtraPumps+=behind;
  }
  if(fill<advFillMin)advFillMin=fill;
  if(fill>advFillMax)advFillMax=fill;
 }
 /* One pump is one Nitro sound interval: 174592 ARM7 cycles, 5.21 ms of DS time, 83 samples at
  * 16 kHz. The caller reports a frame's real length clamped to 66 ms, which is 12.8 pumps, and the
  * correction above can add twelve more -- so a budget of ten could not cover a slow frame, and this
  * port's frames were slow. Twenty-eight covers the worst frame the caller will report together with
  * the largest catch-up, and still bounds what one call can do. */
 unsigned budget=28;
 while(cyclePending>=VITAPOKE_AUDIO_PUMP_CYCLES&&budget){VitaNativeSoundPump();cyclePending-=VITAPOKE_AUDIO_PUMP_CYCLES;budget--;advRanPumps++;}
 /* What is left is a debt, not a mistake: DS time the sound engine owes, which the next frame can
  * pay off. Discarding it -- which is what this did -- meant every slow frame permanently lost the
  * audio it should have produced, so the ring never refilled and better than half of every output
  * chunk came out part silent. It is carried instead, capped at eight pumps so that a genuine stall
  * still cannot build a backlog that takes seconds of fast-forwarded sound to clear. */
 if(cyclePending>VITAPOKE_AUDIO_PUMP_CYCLES*8u)cyclePending=VITAPOKE_AUDIO_PUMP_CYCLES*8u;
}
void VitaNativeSoundOutputLine(char*buf,unsigned len){
unsigned fill=0,under=0,drop=0,written=0;VitaNativeAudioOutStats(&fill,&under,&drop,&written);
#ifdef VITAPOKE_AUDIO_STATS
 {extern void VitaNativeAudioMixStats(unsigned*,unsigned*,unsigned*,unsigned*);unsigned d=0,sm=0,cb=0,mx=0;VitaNativeAudioMixStats(&d,&sm,&cb,&mx);
  extern void VitaNativeAudioMixStats2(unsigned*,unsigned*,unsigned*);unsigned dt[5],bt[5],q[3];VitaNativeAudioMixStats2(dt,bt,q);
  snprintf(buf,len,"[AUDIO] peak=%u clips=%u active=%04x dec=%u smp=%u cb=%u livemax=%u dtype=%u/%u/%u/%u/%u btype=%u/%u/%u/%u/%u quietblk=%u quietdec=%u centre=%u",mixPeak,mixClips,VitaNativeAudioMixActiveMask(),d,sm,cb,mx,dt[0],dt[1],dt[2],dt[3],dt[4],bt[0],bt[1],bt[2],bt[3],bt[4],q[0],q[1],q[2]);return;}
#endif
 {extern unsigned VitaNativeAudioOutLowFill(void);
  unsigned cp=0,ep=0,rp=0,fmin=0,fmax=0,sil=0;
  VitaNativeSoundAdvanceStats(&cp,&ep,&rp,&fmin,&fmax,&sil);
  snprintf(buf,len,"[AUDIO] out=%d fill=%u low=%u written=%u underruns=%u dropped=%u peak=%u clips=%u active=%04x pumps=%u+%u/%u advfill=%u..%u silent=%u",outputReady,fill,VitaNativeAudioOutLowFill(),written,under,drop,mixPeak,mixClips,VitaNativeAudioMixActiveMask(),cp,ep,rp,fmin,fmax,sil);}}
BOOL VitaNativeSoundProofValid(void){printf("[AUDIO-STATE] activeSeen=%04x tickPeak=%u pumpMicros=%llu averageUs=%llu\n",activeSeen,tickPeak,pumpMicros,pumps?pumpMicros/pumps:0);extern u32 VitaNativeAudioStateHash(void);printf("[AUDIO-HASH] samples=%08lx state=%08lx\n",(unsigned long)sampleHash,(unsigned long)VitaNativeAudioStateHash());return energy>0&&activeSeen&&tickPeak&&SNDi_SharedWork&&SNDi_SharedWork->finishCommandTag==lists;}
