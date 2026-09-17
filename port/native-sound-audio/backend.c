#include <nitro.h>
#include <nitro/snd/common/work.h>
#include <simulator/sim_audio.h>
#include <stdio.h>
#include <pspkernel.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define SND_MSG_REQUEST_COMMAND_PROC 0
#define SND_COMMAND_NUM 256
#define UNPACK_COMMAND(v,shift,bits) (((v)>>(shift))&((1ull<<(bits))-1))
static PXIFifoCallback callback;
static BOOL initialized;
static unsigned lists,commands,pumps,activeSeen,tickPeak;static u32 sampleHash=2166136261u;static u64 samples,energy,pumpMicros,clockRemainder,cyclePending;
extern void SNDi_SetPlayerParam7(int,u32,u32,int);
extern void SNDi_SetTrackParam7(int,u32,u32,u32,int);
extern void SNDi_SetSurroundDecay7(int);
extern void SND_LockChannel7(u32,u32);extern void SND_UnlockChannel7(u32,u32);extern void SND_StopUnlockedChannel7(u32,u32);
extern void SND_SetMasterPan7(int);
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
#ifdef VITAPOKE_SAS
 {extern int VitaNativeSasInit(void);extern void VitaNativeMemLog(const char*,...);int ok=VitaNativeSasInit()==0;VitaNativeMemLog("[AUDIO] init output=%s (sceSasCore)",ok?"ready":"unavailable");}
#endif
}
void PXI_InitFifo(void){VitaNativeSoundInit();}
void PXI_SetFifoRecvCallback(int tag,PXIFifoCallback fn){if(tag!=PXI_FIFO_TAG_SOUND){printf("[AUDIO] unsupported FIFO callback %d\n",tag);abort();}callback=fn;VitaNativeSoundInit();}
BOOL PXI_IsCallbackReady(int tag,PXIProc proc){return tag==PXI_FIFO_TAG_SOUND&&initialized;}
int PXI_SendWordByFifo(int tag,u64 data,BOOL error){
 if(tag!=PXI_FIFO_TAG_SOUND||error){printf("[AUDIO] unsupported FIFO send %d\n",tag);return -1;}
 if(data==SND_MSG_REQUEST_COMMAND_PROC)return 0;
 if(data<4096||data>UINT32_MAX||(data&3))abort();
 const SNDCommand*p=(const SNDCommand*)(uintptr_t)data;unsigned limit=0;
 while(p){if(++limit>SND_COMMAND_NUM)abort();SNDCommand command=*p;commands++;
 #include "consumer_switch.inc"
 p=command.next;
 }
 if(!SNDi_SharedWork)abort();SNDi_SharedWork->finishCommandTag++;lists++;return 0;
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
#ifdef VITAPOKE_SAS
   SIM_Audio_AdvanceChannelSilent(87296,256,ch);   /* channel timers count at 16756991/s: 87296 per 5.21 ms pump */
#else
   SIM_Audio_AdvanceChannelSilent(174592,512,ch);
#endif
#endif
  }
  samples+=(174592/512)*16;
 }else
 for(unsigned left=174592;left;){
  if(!active){samples+=((left+511)/512)*16;break;}
  unsigned n=left>512?512:left;
  for(unsigned pending=active;pending;){
   unsigned ch=__builtin_ctz(pending);pending&=pending-1;
   s32 v=SIM_Audio_RunChannel(n,ch);
   if(v){energy+=(u32)(v<0?-v:v);sampleHash=(sampleHash^(u32)v^(u32)(samples+ch))*16777619u;}
   if(!(s_SIM_sndcnt[ch]&(1u<<31)))active&=~(1u<<ch);
  }
  samples+=16;left-=n;
 }
#ifdef VITAPOKE_AUDIO_PROFILE
 t1=sceKernelGetSystemTimeWide();tLoop+=t1-t0;t0=t1;
#endif
#ifdef VITAPOKE_SAS
 {extern void VitaNativeSasSnapshot(u32);VitaNativeSasSnapshot(174592);}
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

void VitaNativeSoundAdvance(u32 microseconds){clockRemainder+=(u64)microseconds*OS_SYSTEM_CLOCK;cyclePending+=clockRemainder/1000000;clockRemainder%=1000000;while(cyclePending>=SND_PROC_INTERVAL*64u){VitaNativeSoundPump();cyclePending-=SND_PROC_INTERVAL*64u;}}
BOOL VitaNativeSoundProofValid(void){printf("[AUDIO-STATE] activeSeen=%04x tickPeak=%u pumpMicros=%llu averageUs=%llu\n",activeSeen,tickPeak,pumpMicros,pumps?pumpMicros/pumps:0);extern u32 VitaNativeAudioStateHash(void);printf("[AUDIO-HASH] samples=%08lx state=%08lx\n",(unsigned long)sampleHash,(unsigned long)VitaNativeAudioStateHash());return energy>0&&activeSeen&&tickPeak&&SNDi_SharedWork&&SNDi_SharedWork->finishCommandTag==lists;}
