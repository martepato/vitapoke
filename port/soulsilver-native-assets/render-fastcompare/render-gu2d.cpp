// GE-based DS 2D compositor for the native Platinum build.
//
// Text backgrounds and tiled sprites are drawn by the PSP GE from 4bpp/8bpp CLUT
// atlases rebuilt from DS VRAM each frame; the GE 3D output buffer is composited
// as a texture (no readback). Per engine, anything not handled (affine/bitmap BGs,
// windows, VRAM display mode) falls back to the software compositor for that frame.
#include "../native-render-opt/native_render.h"
#include "../native-render-opt/native_gpu.h"
#include <nitro.h>
#include <simulator/g3_handler.h>
#include <simulator/g3_draw.h>
#include <pspkernel.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <malloc.h>
#include <array>
extern "C" void VitaNativeMemLog(const char*,...);

static constexpr auto alphaTo5=[](){std::array<u32,256>a{};for(unsigned i=0;i<256;i++)a[i]=((i*31+127)/255)<<24;return a;}();
static unsigned __attribute__((aligned(16))) list[262144];
static unsigned highWater;
extern "C" void* VitaNativeGUGetMemory(unsigned bytes){
 unsigned used=sceGuCheckList();if(used+bytes+4096>sizeof(list)){printf("[GU-LIST] exhausted used=%u request=%u capacity=%u\n",used,bytes,(unsigned)sizeof(list));abort();}
 unsigned total=used+bytes;if(total>highWater)highWater=total;
 return sceGuGetMemory(bytes);
}

static u32 raw[2][256*256] __attribute__((aligned(64)));
static GPU2D::Unit engineA(0),engineB(1);static GPU2D::SoftRenderer renderer;
static bool initialized=false,openFrame=false;static unsigned frames=0,lastDraw=0;
static unsigned inputKeys=0;static int mode=0,down=0,x=128,y=96;
static unsigned bindUs=0,drawUs=0,convertUs=0,displayOffset=0,lastReadbackUs=0,last2DUs=0;
// GE wait profile (perf port): time the CPU spends in sceGuSync (hardware-only cost, ~0 under PPSSPP) and mid-frame texture fences.
static unsigned geSyncUs=0,geSync3DUs=0,geFences=0;
extern "C" void VitaNativeRenderGeProfile(unsigned*syncUs,unsigned*sync3DUs,unsigned*fences){if(syncUs)*syncUs=geSyncUs;if(sync3DUs)*sync3DUs=geSync3DUs;if(fences)*fences=geFences;geSyncUs=geSync3DUs=geFences=0;}
#ifdef OPT_GE_ASYNC
/* OPT_GE_ASYNC: count every sceGuFinish we send; the GE FINISH callback (interrupt) counts
   completions. The k-th callback proves the k-th FINISH executed (lists run in queue order). */
static volatile unsigned geFinishDone=0;static unsigned geFinishSent=0;static unsigned asSyncErr=0;
static inline void GuFinishCounted(){sceGuFinish();geFinishSent++;}
static void AsyncInit();
extern "C" unsigned G3ListPendingFreeHigh();
static inline void GeSyncTimed(unsigned&acc){unsigned t=sceKernelGetSystemTimeLow();if(sceGuSync(0,0)<0)asSyncErr++;acc+=sceKernelGetSystemTimeLow()-t;}
#else
static inline void GuFinishCounted(){sceGuFinish();}
static inline void GeSyncTimed(unsigned&acc){unsigned t=sceKernelGetSystemTimeLow();sceGuSync(0,0);acc+=sceKernelGetSystemTimeLow()-t;}
#endif

// On-screen placement of the two DS panels (same as the Platinum renderer): main fills the full height
// (363x272, ~1.42x nearest, no black bars), touch panel 117x88 centred in the remaining width. Stylus mode
// swaps which panel gets the big slot. The bottom-right corner is left free for the DEV fps counter.
enum{MAIN_X=0,MAIN_Y=0,MAIN_W=363,MAIN_H=272,SUB_X=363,SUB_Y=92,SUB_W=117,SUB_H=88};
static unsigned statFallback[2],statGe[2];
#include "native_window_rows.h"
extern "C" int SSNativeCaptureWindowRows(SSNativeWindowRow rows[2][192]) __attribute__((weak));
static SSNativeWindowRow windowRows[2][192];
static bool rowWindows;
extern "C" char VitaNativeOverlayText[128];
char VitaNativeOverlayText[128];
static bool overlayInit=false;
extern "C" void VitaNativeG3Release();
extern "C" void VitaNativeG3TexturesComplete();
extern "C" void G3ListPendingFreeDrain();
extern "C" void VitaNativeGUTextureFence(){
 // Restart the command list without resetting GE registers or clearing buffers.
 // Pending vertices were copied into the submitted list before this fence.
 if(!openFrame){printf("[TEXTURE] fence outside frame\n");abort();}
 geFences++;GuFinishCounted();
#ifdef OPT_GE_ASYNC
 {extern unsigned asFenceUs;GeSyncTimed(asFenceUs);}
#else
 GeSyncTimed(geSyncUs);
#endif
VitaNativeG3TexturesComplete();G3ListPendingFreeDrain();
 // sceGuStart re-emits the SDK default framebuffer, not DrawBufferList's active target.
 unsigned fb=sceGeGetCmd(0x9c)&0x00ffffffu,fbw=sceGeGetCmd(0x9d)&0x00ffffffu;
 unsigned format=sceGeGetCmd(0xd2)&3u;fb|=(fbw&0x00ff0000u)<<8;
 sceGuStart(GU_DIRECT,list);
 sceGuDrawBufferList(format,(void*)(uintptr_t)fb,fbw&0xffffu);
}

extern "C" unsigned char VitaNative_GfxRegisters[];
static unsigned reg16(unsigned off){return *(volatile u16*)(VitaNative_GfxRegisters+off);}
static unsigned reg32(unsigned off){return *(volatile u32*)(VitaNative_GfxRegisters+off);}

// VRAM layout (byte offsets into EDRAM): 0/0x88000 display, 0x110000 3D colour, 0x140000 3D depth
static const u32 VRAM_3D=0x110000;
static const u32 VRAM_ENGINE[2]={0x158000,0x188000};   // 256x192x4 each, stride 256

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
static void FrameStart(GPU2D::Unit&u){
 u.VBlankEnd();
 u.Win0Active=(u.Win0Coords[2]>u.Win0Coords[3]?1:0)|(u.Win0Coords[0]>u.Win0Coords[1]?2:0);
 u.Win1Active=(u.Win1Coords[2]>u.Win1Coords[3]?1:0)|(u.Win1Coords[0]>u.Win1Coords[1]?2:0);
}

// ---------------------------------------------------------------- atlases
// A 4bpp atlas holds 1024 8x8 tiles as a 128x512 T4 texture (64 B per row); an 8bpp
// atlas holds 1024 tiles as 128x512 T8 (128 B per row). Tile n sits at (n&15)*8, (n>>4)*8.
struct Atlas{u8*data;u32 key;u16 stamp[1024];bool bpp8;u32 gen;u32 capacity;const u8*source;u32 tilebase,sourceMask;};
static Atlas bgAtlas[2][4];          // one per BG layer per frame
static Atlas objAtlas4[2][8];     // per engine: OBJ VRAM 256 KB / 32 KB pages of 4bpp tiles
static Atlas objAtlas8[2][4];     // per engine: 64 KB pages of 8bpp tiles
static u32 frameGen=1;
static void AdvanceAtlasFrame(){
 if((++frameGen&0xFFFF)!=0)return;
 // A tile may be unused for a full 16-bit generation cycle. Invalidate it
 // before reusing generation 1, rather than accepting its old graphics.
 for(auto&engine:objAtlas4)for(auto&a:engine)memset(a.stamp,0,sizeof(a.stamp));
 for(auto&engine:objAtlas8)for(auto&a:engine)memset(a.stamp,0,sizeof(a.stamp));
 for(auto&engine:bgAtlas)for(auto&a:engine)a.gen=0;
 ++frameGen;
}
extern "C" void VitaNativeG3TrimCache();
static unsigned atlasBytes;
static u8*AtlasData(Atlas&a,unsigned bytes=65536){
 if(a.data){if(a.capacity!=bytes){printf("[ATLAS] inconsistent capacity\n");abort();}return a.data;}
 a.data=(u8*)memalign(64,bytes);
 if(!a.data){VitaNativeG3TrimCache();a.data=(u8*)memalign(64,bytes);}
 if(!a.data){printf("[ATLAS] allocation failed bytes=%u live=%u\n",bytes,atlasBytes);abort();}
 a.capacity=bytes;atlasBytes+=bytes;memset(a.data,0,bytes);return a.data;
}
static void ReleaseAtlases(){
 for(auto&engine:bgAtlas)for(auto&a:engine){free(a.data);a={};}
 for(auto&engine:objAtlas4)for(auto&a:engine){free(a.data);a={};}
 for(auto&engine:objAtlas8)for(auto&a:engine){free(a.data);a={};}
 atlasBytes=0;
}

static inline void ConvertTile4(u8*atlas,u32 n,const u8*src){u8*d=atlas+((n>>4)*8*64)+((n&15)*4);for(int r=0;r<8;r++)*(u32*)(d+r*64)=*(const u32*)(src+r*4);}
static inline void ConvertTile8(u8*atlas,u32 n,const u8*src){u8*d=atlas+((n>>4)*8*128)+((n&15)*8);for(int r=0;r<8;r++){*(u32*)(d+r*128)=*(const u32*)(src+r*8);*(u32*)(d+r*128+4)=*(const u32*)(src+r*8+4);}}
// whole-charset conversion for a BG (1024 tiles from tilesetaddr, wrapping in VRAM)
static u8*BuildBGAtlas(int engine,int slot,const u8*vram,u32 mask,u32 tilesetaddr,bool bpp8){
 Atlas*slots=bgAtlas[engine];Atlas&a=slots[slot];
 auto matches=[&](const Atlas&v){return v.gen==frameGen&&v.source==vram&&v.tilebase==tilesetaddr&&v.sourceMask==mask&&v.bpp8==bpp8;};
 if(matches(a))return a.data;
 for(int s=0;s<slot;s++)if(matches(slots[s]))return slots[s].data;
 u8*d=AtlasData(a);a.gen=frameGen;a.source=vram;a.tilebase=tilesetaddr;a.sourceMask=mask;a.bpp8=bpp8;
 if(bpp8){for(u32 n=0;n<1024;n++)ConvertTile8(d,n,vram+((tilesetaddr+n*64)&mask));}
 else{for(u32 n=0;n<1024;n++)ConvertTile4(d,n,vram+((tilesetaddr+n*32)&mask));}
 sceKernelDcacheWritebackRange(d,65536);
 return d;
}
// lazy per-tile OBJ conversion; returns atlas data, converting the tile if stale
static u8*ObjTile(int e,u32 byteaddr,bool bpp8,const u8*vram,u32 mask,u32*tileOut){
 if(bpp8){Atlas&a=objAtlas8[e][(byteaddr>>16)&3];u8*d=AtlasData(a);u32 n=(byteaddr>>6)&1023;*tileOut=n;
  if(a.stamp[n]!=(u16)frameGen){a.stamp[n]=(u16)frameGen;ConvertTile8(d,n,vram+(byteaddr&mask&~63u));sceKernelDcacheWritebackRange(d+((n>>4)*8*128),8*128);}return d;}
 Atlas&a=objAtlas4[e][(byteaddr>>15)&7];u8*d=AtlasData(a,32768);u32 n=(byteaddr>>5)&1023;*tileOut=n;
 if(a.stamp[n]!=(u16)frameGen){a.stamp[n]=(u16)frameGen;ConvertTile4(d,n,vram+(byteaddr&mask&~31u));sceKernelDcacheWritebackRange(d+((n>>4)*8*64),8*64);}return d;
}

// ---------------------------------------------------------------- CLUTs
static u16 __attribute__((aligned(16))) clutBG4[2][256],clutBG8[2][256],clutOBJ4[2][256],clutOBJ8[2][256],clutExt[256];
static void BuildClut(u16*dst,const u16*pal,bool banks){for(int i=0;i<256;i++){u16 c=pal[i]&0x7FFF;bool opaque=banks?((i&15)!=0):(i!=0);dst[i]=opaque?(c|0x8000):0;}sceKernelDcacheWritebackRange(dst,512);}
static const u16*curClut=nullptr;static int curClutMask=-1,curBank=-1;
static void UseClut(const u16*clut,int mask,int bank){
 if(curClut!=clut||clut==clutExt){
  const u16*source=clut;
  if(clut==clutExt){u16*snapshot=(u16*)(((uintptr_t)VitaNativeGUGetMemory(512+15)+15)&~(uintptr_t)15);memcpy(snapshot,clut,512);sceKernelDcacheWritebackRange(snapshot,512);source=snapshot;}
  sceGuClutLoad(16,source);curClut=clut;curClutMask=-1;
 }
 if(curClutMask!=mask||curBank!=bank){sceGuClutMode(GU_PSM_5551,0,mask,bank);curClutMask=mask;curBank=bank;}
}
static const void*curTex=nullptr;static int curTexFmt=-1;
static void UseTexture(const void*tex,int fmt){
 if(curTexFmt!=fmt){sceGuTexMode(fmt,0,0,0);curTexFmt=fmt;}
 if(curTex!=tex){sceGuTexImage(0,128,512,128,tex);curTex=tex;}
}

// ---------------------------------------------------------------- vertex batches
struct V{s16 u,v,x,y,z;};
struct Batch{V*v;u32 n;};
static inline void Quad(Batch&b,int x0,int y0,int u0,int v0,int u1,int v1){V*p=b.v+b.n*2;p[0]={(s16)u0,(s16)v0,(s16)x0,(s16)y0,0};p[1]={(s16)u1,(s16)v1,(s16)(x0+8),(s16)(y0+8),0};b.n++;}
static inline void Flush(Batch&b){if(b.n){sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D,b.n*2,0,b.v);b.n=0;}}

// ---------------------------------------------------------------- feature check
static bool IsTextBG(u32 dispcnt,int bg){
 u32 m=dispcnt&7;
 if(bg<2)return true;                       // BG0/BG1 are always text (BG0 may be 3D, handled separately)
 if(bg==2)return m==0||m==1||m==3;
 return m==0;                                // BG3
}
static bool IsAffineTiled(u32 d,int bg){
 if(bg<2)return false;unsigned m=d&7;
 return bg==2?(m==2||m==4||m==5):(m>=1&&m<=5);
}
static bool AffineSupported(const GPU2D::Unit&u,int bg){
 if(!IsAffineTiled(u.DispCnt,bg)||(u.BGCnt[bg]>>14)>1)return false;
 bool extended=(bg==2)?((u.DispCnt&7)==5):((u.DispCnt&7)>=3);
 if(extended&&((u.BGCnt[bg]&0x80)||(u.DispCnt&0x40000000)))return false;
 int i=bg-2;
 return !(u.BGCnt[bg]&0x2040)&&u.BGRotB[i]==0&&u.BGRotC[i]==0&&u.BGRotA[i]>0&&u.BGRotD[i]>0;
}
static bool EngineSupported(const GPU2D::Unit&u){
 u32 d=u.DispCnt;
 if(((d>>16)&3)!=1)return false;             // only normal graphics display mode
 if(!rowWindows&&(d&0x6000)){                   // WIN0/WIN1: only non-wrapping rectangles on the GE path
  if(u.Win0Coords[0]>u.Win0Coords[1]||u.Win0Coords[2]>u.Win0Coords[3])return false;
  if(u.Win1Coords[0]>u.Win1Coords[1]||u.Win1Coords[2]>u.Win1Coords[3])return false;}
 if((d&7)==6)return false;                   // large bitmap mode
 for(int bg=0;bg<4;bg++)if((d&(0x100u<<bg))&&!IsTextBG(d,bg)&&!AffineSupported(u,bg))return false;
 if(u.BGMosaicSize[0]|u.BGMosaicSize[1]){}   // mosaic ignored (approximation)
 return true;
}

// ---------------------------------------------------------------- drawing
static void SetBlendAlpha(bool on,unsigned eva,unsigned evb){
 if(!on){sceGuDisable(GU_BLEND);return;}
 unsigned a=eva>16?16:eva,b=evb>16?16:evb;u32 fa=(a*255/16);u32 fb=(b*255/16);
 sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,fa|(fa<<8)|(fa<<16),fb|(fb<<8)|(fb<<16));
}
static void DrawTextBG(GPU2D::Unit&u,int bg,bool blend){
 u32 d=u.DispCnt;u16 bgcnt=u.BGCnt[bg];
 const u8*vram;u32 mask;u.GetBGVRAM(*(u8**)&vram,mask);
 u32 tileset,tilemap;const u16*pal;
 if(u.Num){tileset=(bgcnt&0x3C)<<12;tilemap=(bgcnt&0x1F00)<<3;pal=(const u16*)&GPU::Palette[0x400];}
 else{tileset=((d&0x07000000)>>8)+((bgcnt&0x3C)<<12);tilemap=((d&0x38000000)>>11)+((bgcnt&0x1F00)<<3);pal=(const u16*)&GPU::Palette[0];}
 bool bpp8=(bgcnt&0x80)!=0,wide=(bgcnt&0x4000)!=0,tall=(bgcnt&0x8000)!=0;
 bool extpal=bpp8&&(d&0x40000000);u32 extslot=((bg<2)&&(bgcnt&0x2000))?(2+bg):bg;
 u32 xoff=u.BGXPos[bg]&0x1FF,yoff=u.BGYPos[bg]&0x1FF;
 u8*atlas=BuildBGAtlas(u.Num,bg,vram,mask,tileset,bpp8);
 UseTexture(atlas,bpp8?GU_PSM_T8:GU_PSM_T4);
 // gather tiles per palette group
 static V*groupMem[16];static Batch groups[16];
 unsigned groupTiles[16]={};
 for(int ty=0;ty<25;ty++){int my=((yoff>>3)+ty);if(ty*8-int(yoff&7)>=192)break;
  for(int tx=0;tx<33;tx++){int mx=((xoff>>3)+tx);if(tx*8-int(xoff&7)>=256)break;
   u32 off=tilemap+((my&31)<<6)+((mx&31)<<1);if(wide&&(mx&32))off+=0x800;if(tall&&(my&32))off+=wide?0x1000:0x800;
   u16 e=*(const u16*)&vram[off&mask];unsigned g=bpp8?(extpal?(e>>12):0):(e>>12);groupTiles[g]++;
  }}
 for(int g=0;g<16;g++){groups[g].v=groupTiles[g]?(V*)VitaNativeGUGetMemory(sizeof(V)*2*groupTiles[g]):nullptr;groups[g].n=0;}
 for(int ty=0;ty<25;ty++){int my=((yoff>>3)+ty);int sy=ty*8-(yoff&7);if(sy>=192)break;
  for(int tx=0;tx<33;tx++){int mx=((xoff>>3)+tx);int sx=tx*8-(xoff&7);if(sx>=256)break;
   u32 off=tilemap+((my&31)<<6)+((mx&31)<<1);if(wide&&(mx&32))off+=0x800;if(tall&&(my&32))off+=wide?0x1000:0x800;
   u16 e=*(const u16*)&vram[off&mask];u32 tile=e&0x3FF;int u0=(tile&15)*8,v0=(tile>>4)*8,u1=u0+8,v1=v0+8;
   if(e&0x400){int t=u0;u0=u1;u1=t;}if(e&0x800){int t=v0;v0=v1;v1=t;}
   int g=bpp8?(extpal?(e>>12):0):(e>>12);
   Quad(groups[g],sx,sy,u0,v0,u1,v1);
  }}
 if(blend&&u.EVA==0&&u.EVB>=16)return;          // fully transparent first target: nothing to draw
 SetBlendAlpha(blend,u.EVA,u.EVB);
 for(int g=0;g<16;g++){if(!groups[g].n)continue;
  if(bpp8){if(extpal){BuildClut(clutExt,u.GetBGExtPal(extslot,g),false);UseClut(clutExt,0xFF,0);curClut=nullptr;}else UseClut(clutBG8[u.Num],0xFF,0);}
  else UseClut(clutBG4[u.Num],0x0F,g);
  Flush(groups[g]);}
 (void)groupMem;
}
// Axis-aligned affine tiled backgrounds. Partition destination pixels by source
// tile, so even a nearly edge-on card needs only visible tile rectangles.
static void DrawAffineBG(GPU2D::Unit&u,int bg,bool blend){
 u32 d=u.DispCnt;u16 cnt=u.BGCnt[bg];int i=bg-2;
 const u8*vram;u32 mask;u.GetBGVRAM(*(u8**)&vram,mask);
 u32 base=(cnt&0x3C)<<12,map=(cnt&0x1F00)<<3;
 if(!u.Num){base+=(d&0x07000000)>>8;map+=(d&0x38000000)>>11;}
 bool ext=(bg==2)?((d&7)==5):((d&7)>=3),ep=ext&&(d&0x40000000);
 int size=128<<(cnt>>14),a=u.BGRotA[i],dd=u.BGRotD[i];
 int rx=u.BGXRef[i],ry=u.BGYRef[i];bool identity=a==256&&dd==256;
 if(!identity){
  // Exact DS nearest-neighbor source sampling. GE sprite/triangle fast paths
  // disagree at some fractional UV boundaries; upload indices, not RGBA.
  Atlas&storage=bgAtlas[u.Num][bg];u8*pixels=AtlasData(storage);storage.gen=0;
  for(int y=0;y<192;y++){
   int qy=ry+y*dd;u8*row=pixels+y*256;
   if(qy<0||qy>=size*256){memset(row,0,256);continue;}
   unsigned maprow=(qy>>11)*(size/8);int py=(qy>>8)&7;
   for(int x=0,qx=rx;x<256;x++,qx+=a){
    if(qx<0||qx>=size*256){row[x]=0;continue;}
    unsigned off=maprow+(qx>>11);u16 e=ext?*(const u16*)&vram[(map+off*2)&mask]:vram[(map+off)&mask];
    int px=(qx>>8)&7,yy=py;if(ext&&(e&0x400))px=7-px;if(ext&&(e&0x800))yy=7-yy;
    row[x]=vram[(base+(ext?(e&1023):e)*64+yy*8+px)&mask];
   }
  }
  sceKernelDcacheWritebackRange(pixels,256*192);
  if(blend&&u.EVA==0&&u.EVB>=16)return;SetBlendAlpha(blend,u.EVA,u.EVB);UseClut(clutBG8[u.Num],255,0);
  sceGuTexMode(GU_PSM_T8,0,0,0);sceGuTexImage(0,256,256,256,pixels);sceGuTexFlush();curTex=nullptr;curTexFmt=-1;
  V*v=(V*)VitaNativeGUGetMemory(2*sizeof(V));v[0]={0,0,0,0,0};v[1]={256,192,256,192,0};
  sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,0,v);return;
 }

 struct Run{int begin,end,tile;};Run xr[256],yr[192];int nx=0,ny=0;
 for(int x=0;x<256;){int q=rx+x*a;if(q<0||q>=size*256){x++;continue;}int t=q>>11,e=x+1;while(e<256&&((rx+e*a)>>11)==t)e++;xr[nx++]={x,e,t};x=e;}
 for(int y=0;y<192;){int q=ry+y*dd;if(q<0||q>=size*256){y++;continue;}int t=q>>11,e=y+1;while(e<192&&((ry+e*dd)>>11)==t)e++;yr[ny++]={y,e,t};y=e;}
 if(!nx||!ny)return;
 u8*atlas=BuildBGAtlas(u.Num,bg,vram,mask,base,true);UseTexture(atlas,GU_PSM_T8);
 if(blend&&u.EVA==0&&u.EVB>=16)return;SetBlendAlpha(blend,u.EVA,u.EVB);
 unsigned counts[16]={};
 for(int y=0;y<ny;y++)for(int x=0;x<nx;x++){unsigned off=yr[y].tile*(size/8)+xr[x].tile;u16 e=ext?*(const u16*)&vram[(map+off*2)&mask]:vram[(map+off)&mask];counts[ep?e>>12:0]++;}
 V*verts[16];unsigned used[16]={};for(int g=0;g<16;g++)verts[g]=counts[g]?(V*)VitaNativeGUGetMemory(counts[g]*2*sizeof(V)):nullptr;
 for(int y=0;y<ny;y++)for(int x=0;x<nx;x++){
  Run xx=xr[x],yy=yr[y];unsigned off=yy.tile*(size/8)+xx.tile;u16 e=ext?*(const u16*)&vram[(map+off*2)&mask]:vram[(map+off)&mask];unsigned tile=ext?(e&1023):e,g=ep?e>>12:0;
  int x0=((rx+xx.begin*a)>>8)-xx.tile*8,y0=((ry+yy.begin*dd)>>8)-yy.tile*8;
  int x1=x0+xx.end-xx.begin,y1=y0+yy.end-yy.begin;
  if(ext&&(e&0x400)){x0=8-x0;x1=8-x1;}if(ext&&(e&0x800)){y0=8-y0;y1=8-y1;}
  int tu=(tile&15)*8,tv=(tile>>4)*8;V*v=verts[g]+2*used[g]++;
  v[0]={(s16)(tu+x0),(s16)(tv+y0),(s16)xx.begin,(s16)yy.begin,0};v[1]={(s16)(tu+x1),(s16)(tv+y1),(s16)xx.end,(s16)yy.end,0};
 }
 for(int g=0;g<16;g++){if(!used[g])continue;if(ep){BuildClut(clutExt,u.GetBGExtPal(bg,g),false);UseClut(clutExt,255,0);curClut=nullptr;}else UseClut(clutBG8[u.Num],255,0);
  sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D,used[g]*2,0,verts[g]);}

}
static void Draw3DLayer(bool blend){
 sceGuTexMode(GU_PSM_8888,0,0,0);curTexFmt=GU_PSM_8888;sceGuTexImage(0,256,256,256,(u8*)sceGeEdramGetAddr()+VRAM_3D);curTex=nullptr;
 /* 3D colour is premultiplied by coverage (alpha-3D fix, see g3_backend.cpp DrawVertices): src*1 + dst*(1-a). */
 sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_FIX,GU_ONE_MINUS_SRC_ALPHA,0xFFFFFF,0);
 V*v=(V*)VitaNativeGUGetMemory(2*sizeof(V));v[0]={0,0,0,0,0};v[1]={256,192,256,192,0};
 sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,0,v);
 sceGuDisable(GU_BLEND);curTexFmt=-1;
 (void)blend;
}
static const s32 spritewidth[16]={8,16,8,8,16,32,8,8,32,32,16,8,64,64,32,8};
static const s32 spriteheight[16]={8,8,16,8,16,8,32,8,32,16,32,8,64,32,64,8};
// affine tile: 2 triangles (6 vertices); inv = inverse affine matrix (float), centre = box centre on screen
struct TBatch{V*v;u32 n;};
static inline void FlushT(TBatch&b){if(b.n){sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D,b.n*6,0,b.v);b.n=0;}}
static inline void TileAffine(TBatch&b,float cx,float cy,float tcx,float tcy,const float*inv,int tx0,int ty0,int u0,int v0){
 // texture-space corners of the tile relative to the texture centre -> screen corners
 float px[4],py[4];const int dx[4]={0,8,8,0},dy[4]={0,0,8,8};
 for(int i=0;i<4;i++){float ex=(tx0+dx[i])-tcx,ey=(ty0+dy[i])-tcy;px[i]=cx+inv[0]*ex+inv[1]*ey;py[i]=cy+inv[2]*ex+inv[3]*ey;}
 const int uu[4]={u0,u0+8,u0+8,u0},vv[4]={v0,v0,v0+8,v0+8};const int order[6]={0,1,2,0,2,3};
 V*p=b.v+b.n*6;for(int i=0;i<6;i++){int k=order[i];p[i]={(s16)uu[k],(s16)vv[k],(s16)(px[k]+0.5f),(s16)(py[k]+0.5f),0};}b.n++;
}
static void DrawSprites(GPU2D::Unit&u,int prio,bool blendMode1,bool windowPass){
 u32 d=u.DispCnt;if(!(d&0x1000))return;
 const u16*oam=(const u16*)&GPU::OAM[u.Num?0x400:0];
 const u8*vram;u32 mask;u.GetOBJVRAM(*(u8**)&vram,mask);
 for(int n=127;n>=0;n--){const u16*a=&oam[n*4];
  u32 smode=(a[0]>>10)&3;
  if(windowPass){if(smode!=2)continue;}else{if(smode==2||smode==3)continue;if(((a[2]>>10)&3)!=(u32)prio)continue;}
  bool affine=a[0]&0x100;if(!affine&&(a[0]&0x200))continue;   // disabled
  u32 sizeparam=(a[0]>>14)|((a[1]&0xC000)>>12);int w=spritewidth[sizeparam],h=spriteheight[sizeparam];
  int bw=w,bh=h;if(affine&&(a[0]&0x200)){bw<<=1;bh<<=1;}
  int sx=a[1]&0x1FF;if(sx>=256)sx-=512;int sy=a[0]&0xFF;if(sy+bh>256)sy-=256;
  if(sx>=256||sy>=192||sx+bw<=0||sy+bh<=0)continue;
  bool bpp8=a[0]&0x2000,hf=!affine&&(a[1]&0x1000),vf=!affine&&(a[1]&0x2000);
  u32 tilenum=a[2]&0x3FF;u32 tilesX=w/8,tilesY=h/8;
  if(!windowPass){
   if(bpp8){if(d&0x80000000){BuildClut(clutExt,u.GetOBJExtPal()+(a[2]>>12)*256,false);UseClut(clutExt,0xFF,0);curClut=nullptr;}else UseClut(clutOBJ8[u.Num],0xFF,0);}
   else UseClut(clutOBJ4[u.Num],0x0F,a[2]>>12);
   SetBlendAlpha(blendMode1&&smode==1,u.EVA,u.EVB);
  }else{
   if(bpp8)UseClut(clutOBJ8[u.Num],0xFF,0);else UseClut(clutOBJ4[u.Num],0x0F,a[2]>>12);
  }
  float inv[4]={1,0,0,1};float cx=sx+bw*0.5f,cy=sy+bh*0.5f,tcx=w*0.5f,tcy=h*0.5f;
  if(affine){
   u32 pi=(a[1]>>9)&0x1F;float pa=(s16)oam[pi*16+3]/256.0f,pb=(s16)oam[pi*16+7]/256.0f,pc=(s16)oam[pi*16+11]/256.0f,pd=(s16)oam[pi*16+15]/256.0f;
   float det=pa*pd-pb*pc;if(det==0.0f)continue;float id=1.0f/det;inv[0]=pd*id;inv[1]=-pb*id;inv[2]=-pc*id;inv[3]=pa*id;
   int x0=sx<0?0:sx,y0=sy<0?0:sy,x1=sx+bw>256?256:sx+bw,y1=sy+bh>192?192:sy+bh;sceGuScissor(x0,y0,x1,y1);
  }
  Batch b;b.v=(V*)VitaNativeGUGetMemory(sizeof(V)*2*64);b.n=0;TBatch tb;tb.v=affine?(V*)VitaNativeGUGetMemory(sizeof(V)*6*64):nullptr;tb.n=0;const u8*curAtlas=nullptr;
  for(u32 ty=0;ty<tilesY;ty++)for(u32 tx=0;tx<tilesX;tx++){
   u32 ctx=hf?(tilesX-1-tx):tx,cty=vf?(tilesY-1-ty):ty;
   u32 idx;
   if(d&0x10){idx=(tilenum<<((d>>20)&3))+(((cty*tilesX)+ctx)<<(bpp8?1:0));}
   else{idx=tilenum+cty*32+(ctx<<(bpp8?1:0));}
   u32 byteaddr=idx*32;u32 tn;u8*atlas=ObjTile(u.Num,byteaddr,bpp8,vram,mask,&tn);
   if(atlas!=curAtlas){Flush(b);FlushT(tb);UseTexture(atlas,bpp8?GU_PSM_T8:GU_PSM_T4);curAtlas=atlas;}
   int u0=(tn&15)*8,v0=(tn>>4)*8;
   if(affine)TileAffine(tb,cx,cy,tcx,tcy,inv,tx*8,ty*8,u0,v0);
   else{int u1=u0+8,v1=v0+8;if(hf){int t=u0;u0=u1;u1=t;}if(vf){int t=v0;v0=v1;v1=t;}Quad(b,sx+tx*8,sy+ty*8,u0,v0,u1,v1);}
  }
  Flush(b);FlushT(tb);
  if(affine)sceGuScissor(0,0,256,192);
 }
}
// ---- windows via the GE stencil (alpha channel of the 8888 target) ----
// region codes (one per pixel): 0x10 outside, 0x0F OBJ window (DECR from 0x10), 0x40 WIN1, 0x20 WIN0.
// A layer allowed in a set of regions is tested with NOTEQUAL 0 and the union of the codes' bits.
static bool winActive=false;
static void SetupRowWindows(GPU2D::Unit&u);
static void FillRect(int x0,int y0,int x1,int y1){if(x1<=x0||y1<=y0)return;struct C{u32 c;s16 x,y,z;};C*v=(C*)VitaNativeGUGetMemory(2*sizeof(C));v[0]={0,(s16)x0,(s16)y0,0};v[1]={0,(s16)x1,(s16)y1,0};sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,0,v);}
static void DrawSprites(GPU2D::Unit&u,int prio,bool blendMode1,bool windowPass);
static void SetupWindows(GPU2D::Unit&u){
 if(rowWindows){SetupRowWindows(u);return;}
 u32 d=u.DispCnt;winActive=(d&0xE000)!=0;if(!winActive)return;
 sceGuPixelMask(0x00FFFFFF);                      // stencil (alpha) writes only
 sceGuEnable(GU_STENCIL_TEST);
 if(d&0x8000){sceGuStencilFunc(GU_EQUAL,0x10,0xFF);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_DECR);DrawSprites(u,0,false,true);}
 sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_ALPHA_TEST);
 sceGuStencilOp(GU_REPLACE,GU_REPLACE,GU_REPLACE);
 if(d&0x4000){sceGuStencilFunc(GU_ALWAYS,0x40,0xFF);FillRect(u.Win1Coords[0],u.Win1Coords[2],u.Win1Coords[1],u.Win1Coords[3]);}
 if(d&0x2000){sceGuStencilFunc(GU_ALWAYS,0x20,0xFF);FillRect(u.Win0Coords[0],u.Win0Coords[2],u.Win0Coords[1],u.Win0Coords[3]);}
 sceGuPixelMask(0xFF000000);                      // colour writes only from now on
 sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);
 sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_ALPHA_TEST);
}
static bool InWindowY(unsigned y,unsigned bounds){unsigned first=bounds>>8,last=bounds&255;return first<=last?(y>=first&&y<last):(y>=first||y<last);}
static void FillWindowRow(unsigned bounds,unsigned first,unsigned end){
 unsigned left=bounds>>8,right=bounds&255;
 if(left<=right)FillRect(left,first,right,end);
 else{FillRect(0,first,right,end);FillRect(left,first,256,end);}
}
static void SetupRowWindows(GPU2D::Unit&u){
 winActive=true;
 // Stencil starts as outside marker 0x80; OBJ windows decrement it to 0x7f.
 // Both markers are later replaced by exact per-row layer permission bits.
 sceGuPixelMask(0x00FFFFFF);sceGuEnable(GU_STENCIL_TEST);
 sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_ALPHA_TEST);
 sceGuStencilFunc(GU_ALWAYS,0x80,255);sceGuStencilOp(GU_REPLACE,GU_REPLACE,GU_REPLACE);FillRect(0,0,256,192);
 bool anyObj=false;for(unsigned y=0;y<192;y++)anyObj|=(windowRows[u.Num][y].enable&0x8000)!=0;
 if(anyObj){sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_ALPHA_TEST);sceGuStencilFunc(GU_EQUAL,0x80,255);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_DECR);DrawSprites(u,0,false,true);}
 sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_ALPHA_TEST);
 for(unsigned y=0;y<192;){
  const SSNativeWindowRow&r=windowRows[u.Num][y];unsigned end=y+1;
  bool in0=(r.enable&0x2000)&&InWindowY(y,r.reg[2]),in1=(r.enable&0x4000)&&InWindowY(y,r.reg[3]);
  while(end<192&&!memcmp(&r,&windowRows[u.Num][end],sizeof(r))&&in0==((r.enable&0x2000)&&InWindowY(end,r.reg[2]))&&in1==((r.enable&0x4000)&&InWindowY(end,r.reg[3])))end++;
  unsigned outside=(r.enable&0xe000)?r.reg[5]&63:63;
  unsigned obj=(r.enable&0x8000)?(r.reg[5]>>8)&63:outside;
  sceGuStencilFunc(GU_NOTEQUAL,outside,0x80);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_REPLACE);FillRect(0,y,256,end);
  sceGuStencilFunc(GU_NOTEQUAL,obj,0x40);FillRect(0,y,256,end);
  sceGuStencilOp(GU_REPLACE,GU_REPLACE,GU_REPLACE);
  if(in1){sceGuStencilFunc(GU_ALWAYS,(r.reg[4]>>8)&63,255);FillWindowRow(r.reg[1],y,end);}
  if(in0){sceGuStencilFunc(GU_ALWAYS,r.reg[4]&63,255);FillWindowRow(r.reg[0],y,end);}
  y=end;
 }
 sceGuPixelMask(0xFF000000);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);
 sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_ALPHA_TEST);
}
// allowed-region mask for layer bit `layer` (0-3 BG, 4 OBJ)
static void LayerStencil(GPU2D::Unit&u,int layer){
 if(!winActive){sceGuDisable(GU_STENCIL_TEST);return;}
 if(rowWindows){sceGuEnable(GU_STENCIL_TEST);sceGuStencilFunc(GU_NOTEQUAL,0,1u<<layer);return;}
 u32 d=u.DispCnt;u32 m=0;
 if((d&0x2000)&&(u.WinCnt[0]&(1u<<layer)))m|=0x20;
 if((d&0x4000)&&(u.WinCnt[1]&(1u<<layer)))m|=0x40;
 if((d&0x8000)&&(u.WinCnt[3]&(1u<<layer)))m|=0x0F;
 if(u.WinCnt[2]&(1u<<layer))m|=0x10;
 sceGuEnable(GU_STENCIL_TEST);sceGuStencilFunc(GU_NOTEQUAL,0,m);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);
}
static void DrawBrightness(unsigned mode,unsigned factor){
 if(!mode||!factor)return;if(factor>16)factor=16;u32 a=factor*255/16;u32 col=(mode==1?0x00FFFFFF:0)|(a<<24);
 sceGuDisable(GU_TEXTURE_2D);sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
 struct C{u32 c;s16 x,y,z;};C*v=(C*)VitaNativeGUGetMemory(2*sizeof(C));v[0]={col,0,0,0};v[1]={col,256,192,0};
 sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,0,v);
 sceGuDisable(GU_BLEND);sceGuEnable(GU_TEXTURE_2D);
}
// render one engine with the GE into its VRAM buffer
static void RenderEngineGE(GPU2D::Unit&u){
 int e=u.Num;
 BuildClut(clutBG4[e],(const u16*)&GPU::Palette[e?0x400:0],true);
 BuildClut(clutBG8[e],(const u16*)&GPU::Palette[e?0x400:0],false);
 BuildClut(clutOBJ4[e],(const u16*)&GPU::Palette[e?0x600:0x200],true);
 BuildClut(clutOBJ8[e],(const u16*)&GPU::Palette[e?0x600:0x200],false);
 sceGuDrawBufferList(GU_PSM_8888,(void*)VRAM_ENGINE[e],256);
 sceGuOffset(2048-128,2048-96);sceGuViewport(2048,2048,256,192);sceGuScissor(0,0,256,192);sceGuEnable(GU_SCISSOR_TEST);
 sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_STENCIL_TEST);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_BLEND);sceGuDisable(GU_LIGHTING);
 u16 bd=((const u16*)&GPU::Palette[e?0x400:0])[0];u32 r=(bd&0x1F)<<3,g=((bd>>5)&0x1F)<<3,b=((bd>>10)&0x1F)<<3;
 sceGuPixelMask(0);sceGuClearStencil(0x10);
 sceGuClearColor(0xFF000000|(b<<16)|(g<<8)|r);sceGuClear(GU_COLOR_BUFFER_BIT|GU_STENCIL_BUFFER_BIT);
 sceGuEnable(GU_TEXTURE_2D);sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGBA);sceGuTexFilter(GU_NEAREST,GU_NEAREST);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
 sceGuEnable(GU_ALPHA_TEST);sceGuAlphaFunc(GU_GREATER,0,0xFF);
 curClut=nullptr;curClutMask=-1;curBank=-1;curTex=nullptr;curTexFmt=-1;
 SetupWindows(u);
 u32 d=u.DispCnt;unsigned bmode=(u.BlendCnt>>6)&3;u32 first=u.BlendCnt&0x3F;
 bool blend1=bmode==1;
 for(int prio=3;prio>=0;prio--){
  for(int bg=3;bg>=0;bg--){if(!(d&(0x100u<<bg)))continue;if((u.BGCnt[bg]&3)!=(u32)prio)continue;
   bool bl=blend1&&(first&(1u<<bg));LayerStencil(u,bg);
   if(e==0&&bg==0&&(d&8))Draw3DLayer(bl);else if(IsTextBG(d,bg))DrawTextBG(u,bg,bl);else DrawAffineBG(u,bg,bl);}
  LayerStencil(u,4);DrawSprites(u,prio,blend1,false);
 }
 sceGuDisable(GU_STENCIL_TEST);sceGuPixelMask(0);
 sceGuDisable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);
 if(bmode>=2&&first)DrawBrightness(bmode==2?1:2,u.EVY);
 unsigned mb=u.MasterBrightness;DrawBrightness((mb>>14)&3,mb&0x1F);
 sceGuDisable(GU_TEXTURE_2D);
}
// software fallback for one engine (readback of the 3D buffer only when engine A needs it)
static void RenderEngineSoft(GPU2D::Unit&u,bool need3D){
 if(need3D){const volatile u32*gpu=(volatile u32*)((u8*)sceGeEdramGetAddr()+VRAM_3D);
  for(unsigned i=0;i<256*192;i++){u32 c=gpu[i];GPU3D::NativeFrame[i]=((c>>2)&0x003f3f3fu)|alphaTo5[c>>24];}}
 renderer.SetFramebuffer(raw[0],raw[1]);
 for(unsigned line=0;line<192;line++){
  if(rowWindows){const SSNativeWindowRow&r=windowRows[u.Num][line];
   u.DispCnt=(u.DispCnt&~0xe000u)|(r.enable&0xe000u);
   for(unsigned off=0;off<6;off++)u.Write16(0x40+2*off,r.reg[off]);
   unsigned y0=u.Win0Coords[2],y1=u.Win0Coords[3];u.Win0Active=((y0<=y1?(line>=y0&&line<y1):(line>=y0||line<y1))?1:0)|(u.Win0Coords[0]>u.Win0Coords[1]?2:0);
   y0=u.Win1Coords[2];y1=u.Win1Coords[3];u.Win1Active=((y0<=y1?(line>=y0&&line<y1):(line>=y0||line<y1))?1:0)|(u.Win1Coords[0]>u.Win1Coords[1]?2:0);
  }
  GPU::VCount=line;u.CheckWindows(line);renderer.DrawSprites(line,&u);renderer.DrawScanline(line,&u);}
 sceKernelDcacheWritebackRange(raw[u.Num],256*192*sizeof(u32));
}

extern "C" void VitaNativeRenderSetInput(unsigned keys,int touchMode,int touchDown,int tx,int ty){inputKeys=keys;mode=touchMode;down=touchDown;x=tx;y=ty;}
extern "C" unsigned VitaNativeRenderLastDrawMask(){return lastDraw;}
extern "C" unsigned VitaNativeRenderFrameCount(){return frames;}
extern "C" int VitaNativeRenderInit(){
 if(initialized)return 0;frames=0;displayOffset=0;engineA.Reset();engineB.Reset();sceGuInit();
 sceGuStart(GU_DIRECT,list);sceGuDrawBuffer(GU_PSM_8888,(void*)0,512);sceGuDispBuffer(480,272,(void*)0x88000,512);
 sceGuScissor(0,0,480,272);sceGuEnable(GU_SCISSOR_TEST);sceGuFinish();sceGuSync(0,0);sceGuDisplay(GU_TRUE);
#ifdef OPT_GE_ASYNC
 AsyncInit();
#endif
 G3SIM_MtxMode(GX_MTXMODE_TEXTURE);G3SIM_Identity();G3SIM_MtxMode(GX_MTXMODE_PROJECTION);G3SIM_Identity();G3SIM_MtxMode(GX_MTXMODE_POSITION_VECTOR);G3SIM_Identity();initialized=true;
 printf("[GU2D] GE 2D compositor active\n");return 0;
}
extern "C" int VitaNativeRenderBegin(){
 if(!initialized||openFrame)return -1;openFrame=true;sceGuStart(GU_DIRECT,list);sceGuDrawBufferList(GU_PSM_8888,(void*)VRAM_3D,256);
 sceGuDepthBuffer((void*)0x140000,256);sceGuDepthRange(0,65535);sceGuOffset(2048-128,2048-96);sceGuViewport(2048,2048,256,192);sceGuScissor(0,0,256,192);
 sceGuEnable(GU_SCISSOR_TEST);sceGuEnable(GU_DEPTH_TEST);sceGuDepthFunc(GU_LEQUAL);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_BLEND);sceGuDisable(GU_TEXTURE_2D);
 sceGuEnable(GU_ALPHA_TEST);sceGuAlphaFunc(GU_GREATER,0,0xff); // Reject invisible texels before depth/stencil writes.
 sceGuEnable(GU_STENCIL_TEST);sceGuStencilFunc(GU_ALWAYS,255,255);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_REPLACE);sceGuClearStencil(0);
 sceGuClearColor(0);sceGuClearDepth(65535);sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT|GU_STENCIL_BUFFER_BIT);G3SIM_DrawCleanUp();return 0;
}
extern "C" void VitaNativeOskFrameHook(void) __attribute__((weak));
extern "C" int VitaNativeOskIsActive(void) __attribute__((weak));

#ifdef OPT_GE_ASYNC
/* ---- OPT_GE_ASYNC: pipelined 2D/display pass --------------------------------------------
   Frame N's 2D compositor list is built on its own list buffer (list2D) and handed to the GE
   without sceGuSync; the CPU returns to the game while the GE draws. A flip thread publishes
   the display buffer (and draws the fps overlay into it) once the GE FINISH callback count
   shows that list completed. Frame N+1's Present syncs the whole queue for its 3D list, which
   by queue order includes 2D(N): if the thread has not flipped yet, the main thread flips
   (fallback, counted). Everything the 2D list reads from CPU memory (list2D, atlases, the
   CLUT arrays (clutExt is snapshotted into list memory), raw[] soft engines) is written only inside Present after
   that sync, so no GE-visible memory changes while the list may still execute.
   OSK frames and Present(true) keep the original synchronous path. */
static unsigned __attribute__((aligned(16))) list2D[262144];
unsigned asFenceUs=0;
static bool asyncOn=false;
static SceUID flipEvt=-1,flipLock=-1,flipThread=-1;static volatile int flipQuit=0;
static struct{bool valid;unsigned target,offset,queuedUs;char text[128];}pend;
static unsigned flipVcount=0;
// stats (written under flipLock by both threads, or by main only)
static unsigned asAsync,asSync,asFlipThread,asFlipMain,asGuard,asGuardUs,asLockUs,asLatSum,asLatMax,asList2DHigh,asLateMax,asFinishCbs;
static void GeFinishCallback(int){geFinishDone++;if(flipEvt>=0)sceKernelSetEventFlag(flipEvt,1);}
static void DrawOverlayText(unsigned offset,const char*text){
 if(!text[0])return;
 if(!overlayInit){pspDebugScreenInitEx((void*)0x44000000,PSP_DISPLAY_PIXEL_FORMAT_8888,0);overlayInit=true;}
 pspDebugScreenSetOffset(offset);pspDebugScreenSetXY(67-(int)strlen(text),33);pspDebugScreenSetTextColor(0xFF00FF00);pspDebugScreenSetBackColor(0xFF000000);pspDebugScreenPrintf("%s",text);
}
// Caller holds flipLock and has established that the GE finished pend's list.
static void DoFlipLocked(bool byThread){
 DrawOverlayText(pend.offset,pend.text);
 sceDisplaySetFrameBuf((void*)((u8*)sceGeEdramGetAddr()+pend.offset),512,PSP_DISPLAY_PIXEL_FORMAT_8888,PSP_DISPLAY_SETBUF_NEXTFRAME);
 flipVcount=sceDisplayGetVcount();pend.valid=false;
 unsigned lat=sceKernelGetSystemTimeLow()-pend.queuedUs;asLatSum+=lat;if(lat>asLatMax)asLatMax=lat;
 if(byThread)asFlipThread++;else asFlipMain++;
}
static int FlipThread(SceSize,void*){
 for(;;){
  u32 bits=0;sceKernelWaitEventFlag(flipEvt,1,PSP_EVENT_WAITOR|PSP_EVENT_WAITCLEAR,&bits,nullptr);
  if(flipQuit)break;
  sceKernelWaitSema(flipLock,1,nullptr);
  if(pend.valid&&geFinishDone>=pend.target)DoFlipLocked(true);
  sceKernelSignalSema(flipLock,1);
 }
 return 0;
}
static void AsyncInit(){
 flipLock=sceKernelCreateSema("geflip_lock",0,1,1,nullptr);
 flipEvt=sceKernelCreateEventFlag("geflip_evt",0,0,nullptr);
 if(flipLock>=0&&flipEvt>=0)flipThread=sceKernelCreateThread("geflip",FlipThread,0x14,0x4000,0,nullptr);
 if(flipLock<0||flipEvt<0||flipThread<0||sceKernelStartThread(flipThread,0,nullptr)<0){
  VitaNativeMemLog("[GEASYNC] init FAILED lock=%08x evt=%08x thread=%08x; synchronous presentation",(unsigned)flipLock,(unsigned)flipEvt,(unsigned)flipThread);
  return;}
 geFinishSent=0;geFinishDone=0;
 sceGuSetCallback(GU_CALLBACK_FINISH,GeFinishCallback);
 asyncOn=true;VitaNativeMemLog("[GEASYNC] enabled (flip thread 0x14, list2D %u KB)",(unsigned)(sizeof(list2D)/1024));
}
// Main thread, only right after a full sceGuSync: the GE is idle, so any pending list is done.
static void AsyncSettle(){
 if(!asyncOn)return;
 unsigned t=sceKernelGetSystemTimeLow();sceKernelWaitSema(flipLock,1,nullptr);asLockUs+=sceKernelGetSystemTimeLow()-t;
 {unsigned late=geFinishSent-geFinishDone;if(late>asLateMax)asLateMax=late;}
 if(pend.valid)DoFlipLocked(false);
 sceKernelSignalSema(flipLock,1);
}
#ifdef OPT_GE_ASYNC_VERIFY
/* TEST ONLY: hash every CPU buffer the queued 2D list can read, at queue time and again at the
   next sync point; any difference means GE-visible memory changed while the GE could read it. */
static unsigned verifyHash,verifyUsed,verifyChecks,verifyMismatch;static bool verifyArmed;
static inline unsigned HashBytes(unsigned h,const u8*p,unsigned n){for(unsigned i=0;i<n;i++)h=(h^p[i])*16777619u;return h;}
static unsigned AsyncResourceHash(unsigned used){
 unsigned h=HashBytes(2166136261u,(const u8*)list2D,used);
 for(auto&e:bgAtlas)for(auto&a:e)if(a.data)h=HashBytes(h^1,a.data,a.capacity);
 for(auto&e:objAtlas4)for(auto&a:e)if(a.data)h=HashBytes(h^2,a.data,a.capacity);
 for(auto&e:objAtlas8)for(auto&a:e)if(a.data)h=HashBytes(h^3,a.data,a.capacity);
 h=HashBytes(h,(const u8*)clutBG4,sizeof(clutBG4));h=HashBytes(h,(const u8*)clutBG8,sizeof(clutBG8));
 h=HashBytes(h,(const u8*)clutOBJ4,sizeof(clutOBJ4));h=HashBytes(h,(const u8*)clutOBJ8,sizeof(clutOBJ8));
 h=HashBytes(h,(const u8*)raw,sizeof(raw));
 return h;
}
#endif
static void AsyncLogWindow(){
 sceKernelWaitSema(flipLock,1,nullptr);
 unsigned n=asAsync+asSync;
#ifdef VITAPOKE_DEV
 VitaNativeMemLog("[GEASYNC] presents=%u async=%u sync_osk=%u flip_thread=%u flip_main=%u flip_lat_avg_us=%u flip_lat_max_us=%u ",
  n,asAsync,asSync,asFlipThread,asFlipMain,(asFlipThread+asFlipMain)?asLatSum/(asFlipThread+asFlipMain):0,asLatMax);
 VitaNativeMemLog("[GEASYNC] vblank_guard=%u guard_us=%u lock_us=%u fence_wait_us=%u sync_err=%u finish_sent=%u finish_cb=%u late_max=%u list2d_high=%u list_high=%u",
  asGuard,asGuardUs,asLockUs,asFenceUs,asSyncErr,geFinishSent,(unsigned)geFinishDone,asLateMax,asList2DHigh,highWater);
 {VitaNativeMemLog("[GEASYNC] deferred_free_high=%u",G3ListPendingFreeHigh());}
#else
 (void)n;
#endif

 asAsync=asSync=asFlipThread=asFlipMain=asGuard=asGuardUs=asLockUs=asLatSum=asLatMax=asLateMax=0;asFenceUs=0;
 sceKernelSignalSema(flipLock,1);
}
#endif
extern "C" void G3SIM_FlushDeferred();
static int Present(bool waitForVblank){
 if(!openFrame)return -1;G3SIM_FlushArray();G3SIM_FlushDeferred();if(frames%300==0)printf("[GU-LIST] frame=%u high_water=%u capacity=%u\n",frames,highWater,(unsigned)sizeof(list));GuFinishCounted();GeSyncTimed(geSync3DUs);VitaNativeG3TexturesComplete();G3ListPendingFreeDrain();openFrame=false;
#ifdef OPT_GE_ASYNC
 AsyncSettle();
#ifdef OPT_GE_ASYNC_VERIFY
 if(verifyArmed){verifyArmed=false;verifyChecks++;unsigned h=AsyncResourceHash(verifyUsed);if(h!=verifyHash){verifyMismatch++;printf("[GEASYNC-VERIFY] MISMATCH frame=%u\n",frames);}
  if(verifyChecks%300==0)printf("[GEASYNC-VERIFY] checks=%u mismatches=%u\n",verifyChecks,verifyMismatch);}
#endif
#endif
 unsigned t0=sceKernelGetSystemTimeLow();
 unsigned power=reg16(0x304),a=reg32(0);
 /* PSP port: DS display capture (DISPCAPCNT) and main-memory display mode are not emulated. The legendary cinematic turns capture on for an afterimage effect; skip the capture and keep presenting the normal engines instead of stopping the game. */ if((reg32(0x64)&0x80000000u)||((a>>16)&3)==3){static bool warned;if(!warned){warned=true;printf("[NATIVE-RENDER] display capture/FIFO requested; capture skipped\n");}}
 int snapshot=SSNativeCaptureWindowRows?SSNativeCaptureWindowRows(windowRows):0;
 if(snapshot<0)return -3;rowWindows=snapshot!=0;
 mapMemory();engineA.Enabled=(power&2)!=0;engineB.Enabled=(power&512)!=0;bindRegisters(engineA,0);bindRegisters(engineB,0x1000);
 FrameStart(engineA);FrameStart(engineB);AdvanceAtlasFrame();
 bindUs=sceKernelGetSystemTimeLow()-t0;unsigned t1=sceKernelGetSystemTimeLow();
 bool ge[2];const void*texsrc[2];int texfmt[2];
 GPU2D::Unit*units[2]={&engineA,&engineB};
 // software engines first (they may need the 3D readback and run on the CPU)
 for(int e=0;e<2;e++){GPU2D::Unit&u=*units[e];ge[e]=u.Enabled&&EngineSupported(u);
  if(!ge[e]){RenderEngineSoft(u,e==0&&(u.DispCnt&8)&&((u.DispCnt>>16)&3)==1);statFallback[e]++;texsrc[e]=raw[e];texfmt[e]=GU_PSM_8888;}
  else{statGe[e]++;texsrc[e]=(u8*)sceGeEdramGetAddr()+VRAM_ENGINE[e];texfmt[e]=GU_PSM_8888;}}
#ifdef OPT_GE_ASYNC
 const bool useAsync=asyncOn&&!waitForVblank&&!(VitaNativeOskIsActive&&VitaNativeOskIsActive());
 if(useAsync){
  // The buffer about to be drawn was on screen until the last flip took effect: never let the
  // GE write it before a vblank has passed since that flip (only triggers on very short frames).
  if(sceDisplayGetVcount()==flipVcount){unsigned tg=sceKernelGetSystemTimeLow();sceDisplayWaitVblankStart();asGuard++;asGuardUs+=sceKernelGetSystemTimeLow()-tg;}
  sceGuStart(GU_DIRECT,list2D);
 }else sceGuStart(GU_DIRECT,list);
#else
 sceGuStart(GU_DIRECT,list);
#endif
 for(int e=0;e<2;e++)if(ge[e])RenderEngineGE(*units[e]);
 drawUs=sceKernelGetSystemTimeLow()-t1;unsigned t2=sceKernelGetSystemTimeLow();
 lastDraw=3;
 // present both panels
 sceGuDrawBufferList(GU_PSM_8888,(void*)displayOffset,512);
 sceGuOffset(2048-240,2048-136);sceGuViewport(2048,2048,480,272);sceGuScissor(0,0,480,272);sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_STENCIL_TEST);
 sceGuClearColor(0xff101010);sceGuClear(GU_COLOR_BUFFER_BIT);sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGBA);sceGuTexFilter(GU_NEAREST,GU_NEAREST);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
 const bool swap=mode!=0;
 const short bigX=MAIN_X,bigY=MAIN_Y,bigW=MAIN_W,bigH=MAIN_H,smallX=SUB_X,smallY=SUB_Y,smallW=SUB_W,smallH=SUB_H;
 for(unsigned screen=0;screen<2;screen++){unsigned e=(power&0x8000)?screen:1-screen;const bool big=(screen==1)==swap;
  sceGuTexFilter(big?GU_NEAREST:GU_LINEAR,big?GU_NEAREST:GU_LINEAR);sceGuTexImage(0,256,256,256,texsrc[e]);V*v=(V*)VitaNativeGUGetMemory(2*sizeof(V));
  short px=big?bigX:smallX,py=big?bigY:smallY,pw=big?bigW:smallW,ph=big?bigH:smallH;
  v[0]={0,0,px,py,0};v[1]={256,192,short(px+pw),short(py+ph),0};sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,0,v);}
 if(mode){struct C{u32 color;short x,y,z;};C*c=(C*)VitaNativeGUGetMemory(4*sizeof(C));short cx=bigX+(x*bigW)/256,cy=bigY+(y*bigH)/192;u32 col=down?0xff00ffff:0xffffffff;c[0]={col,short(cx-3),cy,0};c[1]={col,short(cx+3),cy,0};c[2]={col,cx,short(cy-3),0};c[3]={col,cx,short(cy+3),0};sceGuDisable(GU_TEXTURE_2D);sceGuDrawArray(GU_LINES,GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,4,0,c);}
 sceKernelDcacheWritebackAll();
#ifdef OPT_GE_ASYNC
 if(useAsync){
  unsigned used=sceGuCheckList();if(used>asList2DHigh)asList2DHigh=used;
  GuFinishCounted();
#ifdef OPT_GE_ASYNC_VERIFY
  verifyUsed=used;verifyHash=AsyncResourceHash(used);verifyArmed=true;
#endif
  sceKernelWaitSema(flipLock,1,nullptr);
  pend.valid=true;pend.target=geFinishSent;pend.offset=displayOffset;pend.queuedUs=sceKernelGetSystemTimeLow();
  memcpy(pend.text,VitaNativeOverlayText,sizeof(pend.text));pend.text[sizeof(pend.text)-1]=0;asAsync++;
  sceKernelSignalSema(flipLock,1);
  sceKernelSetEventFlag(flipEvt,1);
  convertUs=sceKernelGetSystemTimeLow()-t2;last2DUs=sceKernelGetSystemTimeLow()-t0;lastReadbackUs=0;
  displayOffset^=0x88000;frames++;
  if(frames%300==0)printf("[GU2D] frames=%u ge A=%u B=%u soft A=%u B=%u\n",frames,statGe[0],statGe[1],statFallback[0],statFallback[1]);
  if(frames%600==0)AsyncLogWindow();
  return 0;
 }
 if(asyncOn){asSync++;if(frames%600==599)AsyncLogWindow();}
#endif
 GuFinishCounted();GeSyncTimed(geSyncUs);
 if(VitaNativeOverlayText[0]){
  if(!overlayInit){pspDebugScreenInitEx((void*)0x44000000,PSP_DISPLAY_PIXEL_FORMAT_8888,0);overlayInit=true;}
  pspDebugScreenSetOffset(displayOffset);pspDebugScreenSetXY(67-(int)strlen(VitaNativeOverlayText),33);pspDebugScreenSetTextColor(0xFF00FF00);pspDebugScreenSetBackColor(0xFF000000);pspDebugScreenPrintf("%s",VitaNativeOverlayText);
 }
 convertUs=sceKernelGetSystemTimeLow()-t2;last2DUs=sceKernelGetSystemTimeLow()-t0;lastReadbackUs=0;
 /* PSP on-screen keyboard (naming screen substitute), ported from the Platinum renderer:
    ticked once per displayed frame after this frame's GE work is flushed; no buffer flip
    while it is up (it draws into the buffer sceGuDrawBufferList last selected). */
 { bool oskActive=VitaNativeOskIsActive&&VitaNativeOskIsActive();
   if(oskActive&&VitaNativeOskFrameHook)VitaNativeOskFrameHook();
   if(waitForVblank)sceDisplayWaitVblankStart();sceDisplaySetFrameBuf((void*)((u8*)sceGeEdramGetAddr()+displayOffset),512,PSP_DISPLAY_PIXEL_FORMAT_8888,PSP_DISPLAY_SETBUF_NEXTFRAME);if(!oskActive)displayOffset^=0x88000;frames++; }
 if(frames%300==0)printf("[GU2D] frames=%u ge A=%u B=%u soft A=%u B=%u\n",frames,statGe[0],statGe[1],statFallback[0],statFallback[1]);
 return 0;
}
extern "C" void VitaNativeRenderShutdown(){
#ifdef OPT_GE_ASYNC
 if(asyncOn){if(openFrame)GuFinishCounted();sceGuSync(0,0);AsyncSettle();if(openFrame){VitaNativeG3TexturesComplete();openFrame=false;}
  flipQuit=1;sceKernelSetEventFlag(flipEvt,1);sceKernelWaitThreadEnd(flipThread,nullptr);sceKernelDeleteThread(flipThread);
  sceGuSetCallback(GU_CALLBACK_FINISH,nullptr);sceKernelDeleteEventFlag(flipEvt);sceKernelDeleteSema(flipLock);flipEvt=flipLock=flipThread=-1;asyncOn=false;}
#endif
 if(openFrame){sceGuFinish();sceGuSync(0,0);VitaNativeG3TexturesComplete();openFrame=false;}if(initialized){sceGuSync(0,0);VitaNativeG3Release();ReleaseAtlases();sceGuDisplay(GU_FALSE);sceGuTerm();initialized=false;}}
extern "C" unsigned VitaNativeRenderTestPixel(unsigned e,unsigned x,unsigned y){u32 c=__builtin_allegrex_wsbw(raw[e&1][(y%192)*256+(x%256)]);return (c>>8)|(c<<24);}
extern "C" void VitaNativeRenderGetTimings(unsigned*r,unsigned*s){if(r)*r=lastReadbackUs;if(s)*s=last2DUs;}
extern "C" unsigned RenderStage(unsigned stage){return stage==0?bindUs:stage==1?drawUs:convertUs;}
extern "C" int VitaNativeRenderPresent(){return Present(true);}
extern "C" int VitaNativeRenderPresentNoWait(){return Present(false);}
