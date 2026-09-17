#include "native_render.h"
#include <nitro.h>
#include <simulator/g3_handler.h>
#include <pspkernel.h>
#include <psppower.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
PSP_MODULE_INFO("Native mixed renderer",0,1,0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER|THREAD_ATTR_VFPU);
extern "C" fx32 FX_MulFunc(fx32 a,fx32 b){return ((s64)a*b+0x800)>>12;}
extern "C" void SIM_handleAssertionFailure(const char*f,unsigned n,const char*e){printf("[ASSERT] %s:%u %s\n",f,n,e);abort();}
extern "C" unsigned RenderStage(unsigned);
extern "C" unsigned VitaNativeRenderTestPixel(unsigned,unsigned,unsigned);
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
 s_reg_GX_DISPCNT=(1<<16)|(1<<8)|(1<<9)|(1<<3)|(1<<12)|(1<<4);s_reg_G2_BG1CNT=(4<<8)|3;s_reg_GXS_DB_DISPCNT=(1<<16)|(1<<8);
 s_reg_G2_BG0CNT=s_reg_G2S_DB_BG0CNT=(4<<8);
 G2_SetBG0Offset(0,0);G2S_SetBG0Offset(0,0);
}

static void triangle(){G3SIM_Begin(GX_BEGIN_TRIANGLES);G3SIM_PolygonAttr((31u<<16)|(3u<<6));G3SIM_Color(0x03e0);s_texImageParam.textureFormat=0;s_texImageParam.textureSSize=s_texImageParam.textureTSize=8;
 G3SIM_Vtx(-2400,-2200,0);G3SIM_Vtx(2400,-2200,0);G3SIM_Vtx(0,2400,0);
 // Upstream SubmitPolygon checks slot3 W even fortriangles; initialize that unusedslot.
 s_g3PolygonVerts[3].w=4096;G3SIM_SubmitPolygon(s_g3PolygonVerts,3);}
int main(){scePowerSetClockFrequency(333,333,166);scene();s_reg_GX_POWCNT=0x820f;
 s_reg_GX_DISPCNT|=1<<13;s_reg_G2_WIN0H=128;s_reg_G2_WIN0V=192;s_reg_G2_WININ=0x12;s_reg_G2_WINOUT=0x3f;
 VitaNativeRenderInit();unsigned failures=0,skipped=0,active=0;for(unsigned f=0;f<96;f++){
  if(f==1)s_reg_GX_DISPCNT&=~(1<<13);if(f==40)((u16*)s_HW_DB_BG_PLTT)[1]=0x001f;
  if(f==45||f==55)s_reg_G2S_DB_BG0HOFS=1;if(f==46||f==56)s_reg_G2S_DB_BG0HOFS=0;
  if(f==60){memcpy(s_HW_DB_OBJ_VRAM,s_HW_OBJ_VRAM,0x20000);memcpy(s_HW_DB_OBJ_PLTT,s_HW_OBJ_PLTT,512);memcpy(s_HW_DB_OAM,s_HW_OAM,1024);s_reg_GXS_DB_DISPCNT|=(1<<12)|(1<<4);}
  if(f==61)((u16*)s_HW_DB_OAM)[1]=(1<<14)|140;
  if(f==62)s_HW_DB_OBJ_VRAM[0]^=1;
  if(f==63)((u16*)s_HW_DB_OBJ_PLTT)[1]=0x7fff;
  if(f==64)s_reg_GXS_DB_DISPCNT&=~((1<<12)|(1<<4));
  unsigned keys=(f==45)?1:0;int touch=(f>=55&&f<60);VitaNativeRenderSetInput(keys,touch,touch,128,96);
  if(f==85)s_reg_GX_POWCNT^=0x8000;
  unsigned start=sceKernelGetSystemTimeLow();VitaNativeRenderBegin();triangle();if(VitaNativeRenderPresentNoWait())abort();
  unsigned mask=VitaNativeRenderLastDrawMask();if(mask==1)skipped++;else active++;
  if((f==45||f==46||f==55||f==56||(f>=60&&f<=64)||f>=85)&&mask!=3)failures++;
  if(f==40||f==42)printf("[MIXED] palette frame=%u pixel=%08x\n",f,VitaNativeRenderTestPixel(1,89,64));
  if(f==40&&mask!=1)failures++;
  if(f==40&&VitaNativeRenderTestPixel(1,89,64)!=0xfffbfbfb)failures++;
  if(f==42&&(VitaNativeRenderTestPixel(1,89,64)&0xffffff)!=0xfb0000)failures++;
  if(f==0){if((VitaNativeRenderTestPixel(0,100,96)&0xff00)>0x2000)failures++;if((VitaNativeRenderTestPixel(0,128,112)&0xffffff)!=0xfbfb00)failures++;unsigned c=VitaNativeRenderTestPixel(0,128,96);printf("[MIXED] center=%08x\n",c);if((c&0x00ff0000)>0x00200000||(c&0xff00)<0xc000)failures++;}
  if(f==0||f==40||f==42||f==95){unsigned hash=2166136261u;for(unsigned e=0;e<2;e++)for(unsigned y=0;y<192;y++)for(unsigned x=0;x<256;x++)hash=(hash^VitaNativeRenderTestPixel(e,x,y))*16777619u;printf("[PIXELHASH] frame=%u hash=%08x\n",f,hash);}
  if(f==41||f==56||f==57)printf("[STAGES] bind=%u draw=%u convert=%u\n",RenderStage(0),RenderStage(1),RenderStage(2));
  if(f==41||f==56||f==57){unsigned r,s;VitaNativeRenderGetTimings(&r,&s);printf("[MIXED] readback=%uus software2D=%uus\n",r,s);}
  if(f==41||f==56||f==57)printf("[MIXED] frame=%u mask=%u cpu_plus_present_us=%u\n",f,mask,sceKernelGetSystemTimeLow()-start);
 }
 printf("[MIXED] failures=%u bottom_skips=%u full=%u swap_top_live=1\n",failures,skipped,active);sceKernelExitGame();return failures;
}
