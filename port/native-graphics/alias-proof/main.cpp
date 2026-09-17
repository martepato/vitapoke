#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <psprtc.h>
#include <psppower.h>
#include <cstdio>
#include <cstring>
#include <nitro/gx/g2.h>
#include "native_gpu.h"
PSP_MODULE_INFO("Native melon2D probe",0,1,0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER|THREAD_ATTR_VFPU);
extern "C" {
alignas(16) u8 s_HW_BG_VRAM[0x80000]={},s_HW_DB_BG_VRAM[0x20000]={},s_HW_OBJ_VRAM[0x40000]={},s_HW_DB_OBJ_VRAM[0x20000]={};
u8 s_HW_BG_PLTT[512]={},s_HW_OBJ_PLTT[512]={},s_HW_DB_BG_PLTT[512]={},s_HW_DB_OBJ_PLTT[512]={};
u8 s_HW_OAM[1024]={},s_HW_DB_OAM[1024]={};
extern unsigned char VitaNative_GfxRegisters[4224];
}
static unsigned __attribute__((aligned(16))) list[8192];
static u32 __attribute__((aligned(16))) out[2][256*256];
static GPU2D::Unit a(0),b(1);static GPU2D::SoftRenderer renderer;
static void bindNative() {
 memcpy(GPU::Palette,s_HW_BG_PLTT,512);memcpy(GPU::Palette+512,s_HW_OBJ_PLTT,512);
 memcpy(GPU::Palette+1024,s_HW_DB_BG_PLTT,512);memcpy(GPU::Palette+1536,s_HW_DB_OBJ_PLTT,512);
 memcpy(GPU::OAM,s_HW_OAM,1024);memcpy(GPU::OAM+1024,s_HW_DB_OAM,1024);
 for(int engine=0;engine<2;engine++) {
  GPU2D::Unit &u=engine?b:a;
  volatile u8 *r=VitaNative_GfxRegisters+engine*0x1000;
  u.Write32(0,*(volatile u32*)r);
  for(unsigned off=8;off<=0x54;off+=2) {
   if(off==0x28||off==0x2c||off==0x38||off==0x3c){u.Write32(off,*(volatile u32*)(r+off));off+=2;}
   else u.Write16(off,*(volatile u16*)(r+off));
  }
  u.Write16(0x6c,*(volatile u16*)(r+0x6c));
 }
}
static void render() {
 bindNative();a.VBlankEnd();b.VBlankEnd();renderer.SetFramebuffer(out[0],out[1]);
 for(unsigned line=0;line<192;line++) {
  GPU::VCount=line;a.CheckWindows(line);b.CheckWindows(line);
  renderer.DrawSprites(line,&a);renderer.DrawSprites(line,&b);
  renderer.DrawScanline(line,&a);renderer.DrawScanline(line,&b);
 }
}
static void scene() {
 const unsigned char font[8][7]={{30,17,17,30,16,16,16},{15,16,16,14,1,1,30},{17,25,25,21,19,19,17},{14,17,17,31,17,17,17},{31,4,4,4,4,4,4},{14,4,4,4,4,4,14},{17,17,17,17,17,10,4},{31,16,16,30,16,16,31}};
 const int word[10]={0,1,0,-1,2,3,4,5,6,7};
 u16 *pal=(u16*)s_HW_BG_PLTT;pal[1]=0x7fff;pal[2]=0x4400;
 ((u16*)s_HW_OBJ_PLTT)[1]=0x03ff;
 for(int y=0;y<8;y++)for(int x=0;x<8;x++){int c=((x^y)&4)?2:0;s_HW_BG_VRAM[y*4+x/2]|=c<<((x&1)*4);}
 u16*map=(u16*)(s_HW_BG_VRAM+0x2000);
 for(int l=0;l<10;l++)if(word[l]>=0){int tile=l+1;for(int y=0;y<7;y++)for(int x=0;x<5;x++)if(font[word[l]][y]&(1<<(4-x)))s_HW_BG_VRAM[tile*32+y*4+(x+1)/2]|=1<<(((x+1)&1)*4);map[8*32+11+l]=tile;}
 for(int i=0;i<128;i++){((u16*)s_HW_OAM)[i*4]=512;((u16*)s_HW_DB_OAM)[i*4]=512;}
 u16*oam=(u16*)s_HW_OAM;oam[0]=106;oam[1]=(1<<14)|120;oam[2]=0;
 for(int y=0;y<16;y++)for(int x=0;x<16;x++)if((x-7)*(x-7)+(y-7)*(y-7)<55){unsigned addr=(y/8*2+x/8)*32+(y&7)*4+(x&7)/2;s_HW_OBJ_VRAM[addr]|=1<<((x&1)*4);}
 memcpy(s_HW_DB_BG_VRAM,s_HW_BG_VRAM,sizeof(s_HW_DB_BG_VRAM));memcpy(s_HW_DB_BG_PLTT,s_HW_BG_PLTT,512);
 s_reg_GX_DISPCNT=(1<<16)|(1<<8)|(1<<12)|(1<<4);s_reg_GXS_DB_DISPCNT=(1<<16)|(1<<8);
 s_reg_G2_BG0CNT=s_reg_G2S_DB_BG0CNT=(4<<8);
 G2_SetBG0Offset(0,0);G2S_SetBG0Offset(0,0);
}
#include "alias_test.h"
struct Vertex{short u,v,x,y,z;};
int main(){
 scePowerSetClockFrequency(333,333,166);
 a.Reset();b.Reset();a.Enabled=b.Enabled=true;aliasTest();scene();
 sceGuInit();sceGuStart(GU_DIRECT,list);sceGuDrawBuffer(GU_PSM_8888,(void*)0,512);sceGuDispBuffer(480,272,(void*)(512*272*4),512);
 sceGuOffset(2048-240,2048-136);sceGuViewport(2048,2048,480,272);sceGuScissor(0,0,480,272);sceGuEnable(GU_SCISSOR_TEST);sceGuDisable(GU_DEPTH_TEST);
 sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGBA);sceGuTexFilter(GU_NEAREST,GU_NEAREST);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
 sceGuFinish();sceGuSync(0,0);sceGuDisplay(GU_TRUE);
 u64 total=0;unsigned frames=0;
 for(;;){u64 t0,t1;sceRtcGetCurrentTick(&t0);render();sceRtcGetCurrentTick(&t1);total+=t1-t0;
  for(int s=0;s<2;s++)for(int i=0;i<256*192;i++){u32 c=out[s][i];out[s][i]=(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255);}
  sceKernelDcacheWritebackAll();sceGuStart(GU_DIRECT,list);sceGuClearColor(0xff101010);sceGuClear(GU_COLOR_BUFFER_BIT);
  for(int s=0;s<2;s++){sceGuTexImage(0,256,256,256,out[s]);Vertex*v=(Vertex*)sceGuGetMemory(2*sizeof(Vertex));v[0]={0,0,short(s*240),45,0};v[1]={256,192,short((s+1)*240),225,0};sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,0,v);}
  sceGuFinish();sceGuSync(0,0);sceDisplayWaitVblankStart();sceGuSwapBuffers();
  if(++frames%60==0)printf("[NATIVE2D] frames=%u renderer_ms=%.3f (two screens fullres)\n",frames,1000.0*total/sceRtcGetTickResolution()/frames);
 }
}
