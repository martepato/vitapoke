#include "native_render.h"
#include "native_gpu.h"
#include <nitro.h>
#include <simulator/g3_handler.h>
#include <simulator/g3_draw.h>
#include <pspkernel.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspdisplay.h>
#include <cstdio>
#include <cstring>
#include <array>
static constexpr auto alphaTo5=[](){std::array<u32,256>a{};for(unsigned i=0;i<256;i++)a[i]=((i*31+127)/255)<<24;return a;}();
static unsigned __attribute__((aligned(16))) list[262144];
static u32 raw[2][256*256] __attribute__((aligned(64)));
static u32 (&display)[2][256*256]=raw;
static GPU2D::Unit engineA(0),engineB(1);static GPU2D::SoftRenderer renderer;
static bool initialized=false,openFrame=false;static unsigned frames=0,lastDraw=0,previousPower=~0u,grace=0;
static unsigned inputKeys=0,oldKeys=0;static int mode=0,oldMode=0,down=0,oldDown=0,x=128,y=96,oldX=128,oldY=96;
static unsigned bindUs=0,drawUs=0,convertUs=0;
static unsigned displayOffset=0,lastReadbackUs=0,last2DUs=0;
extern "C" void VitaNativeG3Release();
extern "C" unsigned char VitaNative_GfxRegisters[];
static unsigned reg16(unsigned off){return *(volatile u16*)(VitaNative_GfxRegisters+off);}
static unsigned reg32(unsigned off){return *(volatile u32*)(VitaNative_GfxRegisters+off);}
static void bindRegisters(GPU2D::Unit&u,unsigned base){
 u.Write32(0,reg32(base));
 for(unsigned off=8;off<=0x54;off+=2){
  if(off==0x28||off==0x2c||off==0x38||off==0x3c){u.Write32(off,reg32(base+off));off+=2;}
  else u.Write16(off,reg16(base+off));
 }
 u.Write16(0x6c,reg16(base+0x6c));
}
static void mapMemory(){
 memcpy(GPU::Palette,s_HW_BG_PLTT,512);memcpy(GPU::Palette+512,s_HW_OBJ_PLTT,512);
 memcpy(GPU::Palette+1024,s_HW_DB_BG_PLTT,512);memcpy(GPU::Palette+1536,s_HW_DB_OBJ_PLTT,512);
 memcpy(GPU::OAM,s_HW_OAM,1024);memcpy(GPU::OAM+1024,s_HW_DB_OAM,1024);
 GPU::VRAMMap_LCDC=0;for(unsigned i=0;i<4;i++){GPU::VRAM[i]=s_HW_LCDC_VRAM+i*0x20000;if((*(volatile u8*)(VitaNative_GfxRegisters+0x240+i)&0x87)==0x80)GPU::VRAMMap_LCDC|=1u<<i;}
 memset(GPU::VRAMFlat_ABGExtPal,0,32768);memset(GPU::VRAMFlat_BBGExtPal,0,32768);memset(GPU::VRAMFlat_AOBJExtPal,0,8192);memset(GPU::VRAMFlat_BOBJExtPal,0,8192);
 unsigned e=*(volatile u8*)(VitaNative_GfxRegisters+0x244);
 if((e&0x87)==0x84)memcpy(GPU::VRAMFlat_ABGExtPal,s_HW_LCDC_VRAM+0x80000,32768);
 for(unsigned i=0;i<2;i++){unsigned c=*(volatile u8*)(VitaNative_GfxRegisters+0x245+i);const u8*src=s_HW_LCDC_VRAM+0x90000+i*0x4000;
  if((c&0x87)==0x84)memcpy(GPU::VRAMFlat_ABGExtPal+((c&8)?16384:0),src,16384);
  if((c&0x87)==0x85)memcpy(GPU::VRAMFlat_AOBJExtPal,src,8192);
 }
 if((*(volatile u8*)(VitaNative_GfxRegisters+0x248)&0x87)==0x82)memcpy(GPU::VRAMFlat_BBGExtPal,s_HW_LCDC_VRAM+0x98000,32768);
 if((*(volatile u8*)(VitaNative_GfxRegisters+0x249)&0x87)==0x83)memcpy(GPU::VRAMFlat_BOBJExtPal,s_HW_LCDC_VRAM+0xa0000,8192);
}
#include "bottom_cache.h"
#include "top_cache.h"
extern "C" void VitaNativeRenderSetInput(unsigned keys,int touchMode,int touchDown,int tx,int ty){inputKeys=keys;mode=touchMode;down=touchDown;x=tx;y=ty;}
extern "C" unsigned VitaNativeRenderLastDrawMask(){return lastDraw;}
extern "C" unsigned VitaNativeRenderFrameCount(){return frames;}
extern "C" int VitaNativeRenderInit(){
 if(initialized)return 0;frames=0;bottomCache.valid=false;topCache.valid=false;previousPower=~0u;grace=0;displayOffset=0;engineA.Reset();engineB.Reset();sceGuInit();
 sceGuStart(GU_DIRECT,list);sceGuDrawBuffer(GU_PSM_8888,(void*)0,512);sceGuDispBuffer(480,272,(void*)0x88000,512);
 sceGuScissor(0,0,480,272);sceGuEnable(GU_SCISSOR_TEST);sceGuFinish();sceGuSync(0,0);sceGuDisplay(GU_TRUE);
 G3SIM_MtxMode(GX_MTXMODE_TEXTURE);G3SIM_Identity();G3SIM_MtxMode(GX_MTXMODE_PROJECTION);G3SIM_Identity();G3SIM_MtxMode(GX_MTXMODE_POSITION_VECTOR);G3SIM_Identity();initialized=true;return 0;
}
extern "C" int VitaNativeRenderBegin(){
 if(!initialized||openFrame)return -1;openFrame=true;sceGuStart(GU_DIRECT,list);sceGuDrawBufferList(GU_PSM_8888,(void*)0x110000,256);
 sceGuDepthBuffer((void*)0x140000,256);sceGuDepthRange(0,65535);sceGuOffset(2048-128,2048-96);sceGuViewport(2048,2048,256,192);sceGuScissor(0,0,256,192);
 sceGuEnable(GU_SCISSOR_TEST);sceGuEnable(GU_DEPTH_TEST);sceGuDepthFunc(GU_LEQUAL);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_BLEND);sceGuDisable(GU_TEXTURE_2D);
 sceGuEnable(GU_STENCIL_TEST);sceGuStencilFunc(GU_ALWAYS,255,255);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_REPLACE);sceGuClearStencil(0);
 sceGuClearColor(0);sceGuClearDepth(65535);sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT|GU_STENCIL_BUFFER_BIT);G3SIM_DrawCleanUp();return 0;
}
static int Present(bool waitForVblank){
 if(!openFrame)return -1;G3SIM_FlushArray();sceGuFinish();sceGuSync(0,0);openFrame=false;
 // Explicit compatibility fallback: read completed GE3D pixels into the DS
 // software compositor. This preserves2D priority/window/alpha ordering.
 unsigned power=reg16(0x304),a=reg32(0),b=reg32(0x1000);
 unsigned timingStart=sceKernelGetSystemTimeLow();
 const volatile u32*gpu=(volatile u32*)((u8*)sceGeEdramGetAddr()+0x110000);
 if(frames==0)printf("[NATIVE-RENDER] GE center=%08lx\n",gpu[96*256+128]);
 if((power&2)&&((a>>16)&3)==1&&(a&(1<<8))&&(a&(1<<3)))for(unsigned i=0;i<256*192;i++){u32 c=gpu[i];GPU3D::NativeFrame[i]=((c>>2)&0x003f3f3fu)|alphaTo5[c>>24];}
 lastReadbackUs=sceKernelGetSystemTimeLow()-timingStart;timingStart=sceKernelGetSystemTimeLow();
 if((reg32(0x64)&0x80000000u)||((a>>16)&3)==3){printf("[NATIVE-RENDER] unsupported display capture/FIFO\n");return -2;}
 bool changed=inputKeys!=oldKeys||mode!=oldMode||down!=oldDown||x!=oldX||y!=oldY||power!=previousPower;
 if(changed||mode||down)grace=30;else if(grace)grace--;
 // Only throttle engineB while it is the physical bottom. A3D on physical
 // bottom during display swaps stays live; physical top is never throttled.
 bool drawB=frames==0||!(power&0x8000)||grace||frames%3==0;
 bool mappingChanged=((power^previousPower)&0x8000)!=0;
 previousPower=power;oldKeys=inputKeys;oldMode=mode;oldDown=down;oldX=x;oldY=y;
 mapMemory();bool drawA=mappingChanged||!TopSame(power,a);if(drawB&&!mappingChanged&&(power&0x8000)&&BottomSame(power))drawB=false;engineA.Enabled=(power&2)!=0;engineB.Enabled=(power&512)!=0;bindRegisters(engineA,0);bindRegisters(engineB,0x1000);
 bindUs=sceKernelGetSystemTimeLow()-timingStart;unsigned phaseStart=sceKernelGetSystemTimeLow();
 FrameStart(engineA);FrameStart(engineB);renderer.SetFramebuffer(raw[0],raw[1]);
 for(unsigned line=0;line<192;line++){GPU::VCount=line;if(drawA){engineA.CheckWindows(line);renderer.DrawSprites(line,&engineA);renderer.DrawScanline(line,&engineA);}
  if(drawB){engineB.CheckWindows(line);renderer.DrawSprites(line,&engineB);renderer.DrawScanline(line,&engineB);}}
 if(drawA)SaveTop(power,a);if(drawB)SaveBottom(power);
 drawUs=sceKernelGetSystemTimeLow()-phaseStart;phaseStart=sceKernelGetSystemTimeLow();
 lastDraw=(drawA?1:0)|(drawB?2:0);

 convertUs=sceKernelGetSystemTimeLow()-phaseStart;
 last2DUs=sceKernelGetSystemTimeLow()-timingStart;
 for(unsigned e=0;e<2;e++)if(lastDraw&(1u<<e))sceKernelDcacheWritebackRange(display[e],256*192*sizeof(u32));sceGuStart(GU_DIRECT,list);sceGuDrawBufferList(GU_PSM_8888,(void*)displayOffset,512);
 sceGuOffset(2048-240,2048-136);sceGuViewport(2048,2048,480,272);sceGuScissor(0,0,480,272);sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_STENCIL_TEST);
 sceGuClearColor(0xff101010);sceGuClear(GU_COLOR_BUFFER_BIT);sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGBA);sceGuTexFilter(GU_NEAREST,GU_NEAREST);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
 struct V{short u,v,x,y,z;};for(unsigned screen=0;screen<2;screen++){unsigned e=(power&0x8000)?screen:1-screen;sceGuTexImage(0,256,256,256,display[e]);V*v=(V*)sceGuGetMemory(2*sizeof(V));v[0]={0,0,short(screen*240),45,0};v[1]={256,192,short((screen+1)*240),225,0};sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,0,v);}
 if(mode){struct C{u32 color;short x,y,z;};C*c=(C*)sceGuGetMemory(4*sizeof(C));short cx=240+(x*240)/256,cy=45+(y*180)/192;u32 col=down?0xff00ffff:0xffffffff;c[0]={col,short(cx-3),cy,0};c[1]={col,short(cx+3),cy,0};c[2]={col,cx,short(cy-3),0};c[3]={col,cx,short(cy+3),0};sceGuDisable(GU_TEXTURE_2D);sceGuDrawArray(GU_LINES,GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,4,0,c);}
 sceGuFinish();sceGuSync(0,0);if(waitForVblank)sceDisplayWaitVblankStart();sceDisplaySetFrameBuf((void*)((u8*)sceGeEdramGetAddr()+displayOffset),512,PSP_DISPLAY_PIXEL_FORMAT_8888,PSP_DISPLAY_SETBUF_NEXTFRAME);displayOffset^=0x88000;frames++;return 0;
}
extern "C" void VitaNativeRenderShutdown(){if(openFrame){sceGuFinish();sceGuSync(0,0);openFrame=false;}if(initialized){sceGuSync(0,0);VitaNativeG3Release();sceGuDisplay(GU_FALSE);sceGuTerm();initialized=false;}}

extern "C" unsigned VitaNativeRenderTestPixel(unsigned e,unsigned x,unsigned y){u32 c=__builtin_allegrex_wsbw(raw[e&1][(y%192)*256+(x%256)]);return (c>>8)|(c<<24);}

extern "C" void VitaNativeRenderGetTimings(unsigned*r,unsigned*s){if(r)*r=lastReadbackUs;if(s)*s=last2DUs;}

extern "C" unsigned RenderStage(unsigned stage){return stage==0?bindUs:stage==1?drawUs:convertUs;}

extern "C" int VitaNativeRenderPresent(){return Present(true);}
extern "C" int VitaNativeRenderPresentNoWait(){return Present(false);}
