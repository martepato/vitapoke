#include "native_render.h"
#include <stdio.h>
#include <pspkernel.h>
#include <stdlib.h>
#include <malloc.h>
#ifndef VITAPOKE_PROBE_FRAMES
#define VITAPOKE_PROBE_FRAMES 0
#endif
static unsigned frames;static unsigned frameLimit=VITAPOKE_PROBE_FRAMES;
static unsigned long long start,last,gameUs,audioUs,renderUs;
extern void VitaNativeVBlankFrameComplete(void);
extern void VitaNativeSoundAdvance(unsigned elapsedMicroseconds);
extern void VitaNativeInputGetRenderState(unsigned*,int*,int*,int*,int*);
extern void VitaNativeRenderGetTimings(unsigned*,unsigned*);
extern unsigned RenderStage(unsigned);
extern char VitaNativeOverlayText[128];
extern void VitaNativeG3GetProfile(unsigned*,unsigned*);
extern unsigned long long VitaNativeVBlankIdleTake(void);
extern void VitaNativeG3TexProfile(unsigned*,unsigned*,unsigned*,unsigned*,unsigned*);
extern void VitaNativeG3CacheStats(unsigned*,unsigned*,unsigned*,unsigned*,unsigned*,unsigned*,unsigned*);
extern void VitaNativeRenderGeProfile(unsigned*,unsigned*,unsigned*);
extern void VitaNativeG3ListCacheStats(unsigned*,unsigned*,unsigned*,unsigned*,unsigned*,unsigned*,unsigned*);
extern void VitaNativeMemLog(const char*,...);
/* [PERF] card-log line every 600 frames (ported from Platinum native-audio-app, 2026-09-13).
   Only counters the renderer already maintains; per-frame cost is a few adds. */
static struct{unsigned long long game,idle,audio,render,g3,ge3d,ge2d,bind,draw2d;unsigned texBinds,texHits,texMisses,texCmp,texEvict,fences,heapHigh,listHits,listRec,listHitVerts;unsigned long long winStart;}perf;
static void PerfLine(void){
 unsigned n=600;unsigned long long now=sceKernelGetSystemTimeWide(),win=now-perf.winStart;perf.winStart=now;
 unsigned tEnt=0,tBytes=0;VitaNativeG3CacheStats(&tEnt,&tBytes,0,0,0,0,0);unsigned lEnt=0,lBytes=0,lHigh=0,lVol=0;VitaNativeG3ListCacheStats(0,0,&lEnt,&lBytes,0,&lHigh,&lVol);
 VitaNativeMemLog("[PERF] frames=%u fps=%.2f game_busy_us=%llu idle_us=%llu audio_us=%llu render_us=%llu g3_us=%llu ge3d_wait_us=%llu ge2d_wait_us=%llu bind_us=%llu draw2d_us=%llu tex_binds=%u tex_hits=%u tex_decodes=%u tex_evict=%u tex_fences=%u tex_cmp_kb=%u tex_cache=%u/%u list_hits=%u list_rec=%u list_hit_verts=%u list_cache=%u/%u list_high=%u list_volatile=%u list_broken=%u heap_used_high=%u",
  frames,win?n*1000000.0/win:0.0,(perf.game-perf.idle)/n,perf.idle/n,perf.audio/n,perf.render/n,perf.g3/n,perf.ge3d/n,perf.ge2d/n,perf.bind/n,perf.draw2d/n,
  perf.texBinds,perf.texHits,perf.texMisses,perf.texEvict,perf.fences,perf.texCmp/1024,tEnt,tBytes,perf.listHits,perf.listRec,perf.listHitVerts,lEnt,lBytes,lHigh,lVol&0xffff,lVol>>16,perf.heapHigh);
 perf.game=perf.idle=perf.audio=perf.render=perf.g3=perf.ge3d=perf.ge2d=perf.bind=perf.draw2d=0;
 perf.texBinds=perf.texHits=perf.texMisses=perf.texCmp=perf.texEvict=perf.fences=0;perf.heapHigh=0;perf.listHits=perf.listRec=perf.listHitVerts=0;
}
void VitaNativeFrameInit(void){
#if VITAPOKE_PROBE_FRAMES
 FILE *limitFile=fopen("probe-frame-limit.txt","r");if(limitFile){unsigned n;if(fscanf(limitFile,"%u",&n)!=1||!n||n>100000){puts("[SS-PROBE] invalid frame bound");abort();}fclose(limitFile);frameLimit=n;}
#endif
 start=sceKernelGetSystemTimeWide();perf.winStart=start;int result=VitaNativeRenderInit();if(result){printf("[NATIVE] renderer init failed %d\n",result);abort();}if(VitaNativeRenderBegin())abort();}
void VitaNativeFrameComplete(void){
 unsigned long long now=sceKernelGetSystemTimeWide();if(last)gameUs+=now-last;
 VitaNativeSoundAdvance(33333);unsigned long long a=sceKernelGetSystemTimeWide();audioUs+=a-now;
 unsigned keys;int mode,down,x,y;VitaNativeInputGetRenderState(&keys,&mode,&down,&x,&y);VitaNativeRenderSetInput(keys,mode,down,x,y);
 int result=VitaNativeRenderPresentNoWait();if(result){printf("[NATIVE] unsupported renderer state %d\n",result);abort();}
#ifdef VITAPOKE_FRAME_DUMP
 /* TEST ONLY (alpha-3D check): raw engine A, engine B and 3D RGBA buffers at chosen frames. */
 {unsigned f=VitaNativeRenderFrameCount();
  if((f>=VITAPOKE_DUMP_A0&&f<VITAPOKE_DUMP_A1&&!(f%VITAPOKE_DUMP_STEP))||(f>=VITAPOKE_DUMP_B0&&f<VITAPOKE_DUMP_B1&&!(f%VITAPOKE_DUMP_STEP))){
   extern void*sceGeEdramGetAddr(void);char name[64];snprintf(name,64,"dump%05u.raw",f);FILE*fp=fopen(name,"wb");
   if(fp){const unsigned char*b=(const unsigned char*)sceGeEdramGetAddr();fwrite(b+0x158000,1,256*192*4,fp);fwrite(b+0x188000,1,256*192*4,fp);fwrite(b+0x110000,1,256*192*4,fp);fclose(fp);}}}
#endif
 renderUs+=sceKernelGetSystemTimeWide()-a;frames++;
 if(frames%10==0){struct mallinfo mi=mallinfo();if((unsigned)mi.uordblks>perf.heapHigh)perf.heapHigh=mi.uordblks;}
 if(frames%30==0){unsigned long long us=sceKernelGetSystemTimeWide()-start;
#ifdef VITAPOKE_DEV
  {static unsigned long long lastUs;unsigned long long win=us-lastUs;lastUs=us;snprintf(VitaNativeOverlayText,128,"%4.1f fps",win?30000000.0/win:0.0);}
#endif

#ifdef VITAPOKE_DEV
  {unsigned rb=0,twod=0;VitaNativeRenderGetTimings(&rb,&twod);unsigned g3us=0,g3n=0;VitaNativeG3GetProfile(&g3us,&g3n);unsigned long long idle=VitaNativeVBlankIdleTake();
   unsigned tb=0,th=0,tm=0,tc=0,te=0;VitaNativeG3TexProfile(&tb,&th,&tm,&tc,&te);unsigned ge2=0,ge3=0,gf=0;VitaNativeRenderGeProfile(&ge2,&ge3,&gf);
   unsigned lh=0,lr=0,le=0,lb=0,lv=0;VitaNativeG3ListCacheStats(&lh,&lr,&le,&lb,&lv,0,0);
   printf("[NATIVE-FRAME] updates=%u elapsed_us=%llu render=%u average_fps=%.2f window_game_us=%llu idle_us=%llu audio_us=%llu render_us=%llu readback_us=%u bind_us=%u draw2d_us=%u g3_us=%u g3_calls=%u ge3d_wait_us=%u ge2d_wait_us=%u tex_binds=%u tex_hits=%u tex_decodes=%u tex_evict=%u tex_fences=%u tex_cmp_kb=%u list_hits=%u list_rec=%u list_hit_verts=%u list_cache=%u/%u\n",frames,us,VitaNativeRenderFrameCount(),us?frames*1000000.0/us:0.0,gameUs/30,idle/30,audioUs/30,renderUs/30,rb,RenderStage(0),RenderStage(1),g3us/30,g3n/30,ge3/30,ge2/30,tb,th,tm,te,gf,tc/1024,lh,lr,lv,le,lb);
   perf.game+=gameUs;perf.idle+=idle;perf.audio+=audioUs;perf.render+=renderUs;perf.g3+=g3us;perf.ge3d+=ge3;perf.ge2d+=ge2;perf.bind+=RenderStage(0)*30ULL;perf.draw2d+=RenderStage(1)*30ULL;
   perf.texBinds+=tb;perf.texHits+=th;perf.texMisses+=tm;perf.texCmp+=tc;perf.texEvict+=te;perf.fences+=gf;perf.listHits+=lh;perf.listRec+=lr;perf.listHitVerts+=lv;}
#endif

  gameUs=audioUs=renderUs=0;}
#ifdef VITAPOKE_DEV /* on-screen counter and timing lines: DEV builds only */
 if(frames%600==0){unsigned long long us=sceKernelGetSystemTimeWide()-start;VitaNativeMemLog("[FPS] updates=%u average_fps=%.2f",frames,us?frames*1000000.0/us:0.0);PerfLine();}
#endif
 if(frameLimit&&frames>=frameLimit){puts("[NATIVE-FRAME] clean bounded probe exit");sceKernelExitGame();return;}
 VitaNativeVBlankFrameComplete();if(VitaNativeRenderBegin())abort();last=sceKernelGetSystemTimeWide();
}
