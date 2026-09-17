#include "texture_equal.h"
#include <cstdlib>
#include <array>

#include <malloc.h>
extern "C" u8 s_HW_LCDC_VRAM[0xA4000];
extern "C" unsigned char VitaNative_GfxRegisters[];
// Logical DS texture/palette slots can be backed by different physical banks.
// Compare directly on cache hits; gather only on misses, including wired-OR overlaps.
static bool TextureRange(unsigned address,unsigned size,bool palette,u8*dst,const u8*compare){
 while(size){unsigned segment=palette?0x4000:0x20000;unsigned slot=address/segment,within=address&(segment-1),n=segment-within;if(n>size)n=size;
 const u8*src[4];unsigned count=0;
 if(!palette){for(unsigned b=0;b<4;b++){unsigned c=VitaNative_GfxRegisters[0x240+b];if((c&0x87)==0x83&&((c>>3)&3)==slot)src[count++]=s_HW_LCDC_VRAM+b*0x20000+within;}}
 else {unsigned e=VitaNative_GfxRegisters[0x244];if((e&0x87)==0x83&&slot<4)src[count++]=s_HW_LCDC_VRAM+0x80000+slot*0x4000+within;
 for(unsigned b=0;b<2;b++){unsigned c=VitaNative_GfxRegisters[0x245+b],ofs=(c>>3)&3;if((c&0x87)==0x83&&((ofs&1)+((ofs&2)<<1))==slot)src[count++]=s_HW_LCDC_VRAM+0x90000+b*0x4000+within;}}
 if(count==1){if(compare&&!SS_TextureBytesEqual(compare,src[0],n))return false;if(dst)memcpy(dst,src[0],n);}
 else {for(unsigned i=0;i<n;i++){u8 v=0;for(unsigned b=0;b<count;b++)v|=src[b][i];if(compare&&compare[i]!=v)return false;if(dst)dst[i]=v;}}
 if(dst)dst+=n;if(compare)compare+=n;address+=n;size-=n;
 }return true;
}
// A-D texture banks; E/F/G palette banks. Sizes are bytes, not pixels.
static bool TextureFootprint(unsigned format,unsigned w,unsigned h,unsigned offset,unsigned pal,
                             unsigned &texBytes,unsigned &palBytes){
 if(w<8||h<8||w>1024||h>1024||(w&(w-1))||(h&(h-1)))return false;
 unsigned pixels=w*h;
 switch(format){
 case GX_TEXFMT_PLTT4:texBytes=pixels/4;palBytes=8;break;
 case GX_TEXFMT_PLTT16:texBytes=pixels/2;palBytes=32;break;
 case GX_TEXFMT_PLTT256:texBytes=pixels;palBytes=512;break;
 case GX_TEXFMT_A3I5:texBytes=pixels;palBytes=64;break;
 case GX_TEXFMT_A5I3:texBytes=pixels;palBytes=16;break;
 case GX_TEXFMT_DIRECT:texBytes=pixels*2;palBytes=0;break;
 default:return false;
 }
 return offset<=0x80000&&texBytes<=0x80000-offset&&
        (!palBytes||(pal<=0x18000&&palBytes<=0x18000-pal));
}
struct TextureEntry{unsigned offset,pal,format,w,h,color0;u32 *pixels;u8 *snapshot;unsigned texBytes,palBytes;bool queued;unsigned gpuBytes,stride,psm;u32*clut;bool partialAlpha;bool mirS,mirT;unsigned texW,texH;};
static TextureEntry cache[128];static unsigned cacheSize=0,cacheBytes=0,cacheNext=0;
static const unsigned kCacheMaxEntries=128,kCacheMaxBytes=2*1024*1024;
// Perf port from native-stack-render: per-window profile counters (reset by VitaNativeG3TexProfile).
static unsigned cacheEvictions=0;
static unsigned texProfBinds=0,texProfHits=0,texProfMisses=0,texProfCmpBytes=0,texProfEvictions=0;
extern "C" void VitaNativeG3TexProfile(unsigned*binds,unsigned*hits,unsigned*misses,unsigned*cmpBytes,unsigned*evictions){
 if(binds)*binds=texProfBinds;if(hits)*hits=texProfHits;if(misses)*misses=texProfMisses;if(cmpBytes)*cmpBytes=texProfCmpBytes;if(evictions)*evictions=texProfEvictions;
 texProfBinds=texProfHits=texProfMisses=texProfCmpBytes=texProfEvictions=0;}
extern "C" void VitaNativeG3CacheStats(unsigned*entries,unsigned*bytes,unsigned*highBytes,
                                      unsigned*highEntries,unsigned*retries,unsigned*oversize,unsigned*evictions){
 if(entries)*entries=cacheSize;if(bytes)*bytes=cacheBytes;if(highBytes)*highBytes=0;
 if(highEntries)*highEntries=0;if(retries)*retries=0;if(oversize)*oversize=0;if(evictions)*evictions=cacheEvictions;
}
extern "C" void VitaNativeGUTextureFence();
// Called only after sceGuSync has completed every queued texture read.
extern "C" void VitaNativeG3TexturesComplete(){for(unsigned i=0;i<cacheSize;i++)cache[i].queued=false;}
static void EvictOne(){
 if(!cacheSize)return;
 unsigned i=cacheNext++%cacheSize;
 if(cache[i].queued)VitaNativeGUTextureFence();
 TextureEntry&e=cache[i];
 if(e.queued){printf("[TEXTURE] incomplete eviction fence\n");abort();}
 cacheBytes-=e.gpuBytes+e.texBytes+e.palBytes;free(e.pixels);free(e.snapshot);cacheEvictions++;texProfEvictions++;
 if(i!=cacheSize-1)e=cache[cacheSize-1];cache[--cacheSize]={};
}
/* True when the bound texture has texels with 0<alpha<255 (DS translucent texels; alpha-3D fix). */
static bool g3TexPartialAlpha=false;
static void BindTexture(){
 const auto&p=s_texImageParam;g3TexPartialAlpha=false;
 if(p.textureFormat==0){sceGuDisable(GU_TEXTURE_2D);return;}
 if(p.textureFormat==GX_TEXFMT_COMP4x4){printf("[TEXTURE] unsupported format%u (4x4 compressed)\n",p.textureFormat);sceGuDisable(GU_TEXTURE_2D);return;}
 /* DS mirrored repeat (repeat+flip on an axis; ported from Platinum native-stack-render): the GE has no
    mirror mode, so the texture is cached twice as wide/tall with the mirrored copy appended and drawn
    with GU_REPEAT. Through-mode UVs are texels of the DS size (unchanged); raw 3D UVs are normalised, so
    TexScale halves that axis. Doubling past 512, or a downscaled (>512) texture, keeps plain repeat. */
 bool mS=p.repeatS&&p.flipS,mT=p.repeatT&&p.flipT;
 if((mS||mT)&&(TexShift(p.textureSSize,p.textureTSize)||(mS&&p.textureSSize*2>512)||(mT&&p.textureTSize*2>512))){mS=mT=false;static unsigned warned;if(warned<8){warned++;printf("[TEXTURE] mirrored wrap %ux%u exceeds GE size; approximated as repeat\n",p.textureSSize,p.textureTSize);}}
 unsigned texBytes=0,palBytes=0;
 if(!TextureFootprint(p.textureFormat,p.textureSSize,p.textureTSize,p.textureOffset,s_texPlttBase,texBytes,palBytes)){static unsigned warned;if(warned<8){warned++;printf("[TEXTURE] invalid input range fmt=%u %ux%u off=0x%x pal=0x%x; drawing untextured\n",p.textureFormat,p.textureSSize,p.textureTSize,p.textureOffset,s_texPlttBase);}sceGuDisable(GU_TEXTURE_2D);return;}
 TextureEntry *entry=nullptr;texProfBinds++;
 for(unsigned i=0;i<cacheSize;i++){auto&e=cache[i];if(e.offset==p.textureOffset&&e.pal==s_texPlttBase&&e.format==p.textureFormat&&e.w==p.textureSSize&&e.h==p.textureTSize&&e.color0==p.color0&&e.mirS==mS&&e.mirT==mT){texProfCmpBytes+=texBytes+palBytes;if((!palBytes||TextureRange(s_texPlttBase,palBytes,true,nullptr,e.snapshot+texBytes))&&TextureRange(p.textureOffset,texBytes,false,nullptr,e.snapshot)){entry=&e;break;}}}
 if(entry)texProfHits++;else texProfMisses++;
 if(!entry){
  unsigned w=p.textureSSize,h=p.textureTSize,stride=w,psm=GU_PSM_8888,clutBytes=0,sh=TexShift(w,h);
  unsigned W=mS?w*2:w,H=mT?h*2:h,stride0=w; /* mS/mT imply sh==0 */
  if(!sh&&(p.textureFormat==GX_TEXFMT_PLTT4||p.textureFormat==GX_TEXFMT_PLTT16)){psm=GU_PSM_T4;stride0=w<32?32:w;stride=W<32?32:W;clutBytes=16*4;}
  if(!sh&&(p.textureFormat==GX_TEXFMT_PLTT256||p.textureFormat==GX_TEXFMT_A3I5||p.textureFormat==GX_TEXFMT_A5I3)){psm=GU_PSM_T8;stride0=w<16?16:w;stride=W<16?16:W;clutBytes=256*4;}
  if(psm==GU_PSM_8888&&!sh)stride=W;
  unsigned imageBytes=psm==GU_PSM_T4?stride*H/2:psm==GU_PSM_T8?stride*H:(W>>sh)*(H>>sh)*4;
  unsigned imageBytes0=psm==GU_PSM_T4?stride0*h/2:psm==GU_PSM_T8?stride0*h:w*h*4;
  unsigned bytes=imageBytes+clutBytes,totalBytes=bytes+texBytes+palBytes;
  if(totalBytes>kCacheMaxBytes)abort();
  while(cacheSize>=kCacheMaxEntries||(cacheSize&&cacheBytes+totalBytes>kCacheMaxBytes))EvictOne();
  u32*pixels=nullptr;u8*snapshot=nullptr;
  /* Scratch for mirrored composition. Allocated here, with eviction, because after the entry is inserted
     EvictOne could free or move it; a bare malloc there just drew untextured every frame. (>512 textures no
     longer need a full-size buffer: see the row-pair downscale below; the 4 MB heap had no 2 MB block.) */
  const unsigned scratchBytes=(mS||mT)?imageBytes0:0;u8*scratch=nullptr;
  for(;;){
   pixels=(u32*)memalign(16,bytes);snapshot=pixels?(u8*)malloc(texBytes+palBytes):nullptr;
   scratch=snapshot&&scratchBytes?(u8*)malloc(scratchBytes):nullptr;
   if(pixels&&snapshot&&(scratch||!scratchBytes))break;
   free(pixels);free(snapshot);free(scratch);
   if(!cacheSize){
    /* Nothing left to evict. Without the scratch the texture can still be skipped (as before); only a
       failed pixels/snapshot allocation is fatal. The entry has not been inserted yet, so nothing to undo. */
    if(scratchBytes){printf("[TEXTURE] scratch buffer failed %ux%u (%u bytes) maxfree=%u totalfree=%u; drawing untextured\n",w,h,scratchBytes,(unsigned)sceKernelMaxFreeMemSize(),(unsigned)sceKernelTotalFreeMemSize());sceGuDisable(GU_TEXTURE_2D);return;}
    printf("[TEXTURE] allocation failed pixels=%u snapshot=%u\n",bytes,texBytes+palBytes);abort();}
   EvictOne();
  }
  entry=&cache[cacheSize++];*entry={p.textureOffset,s_texPlttBase,p.textureFormat,w,h,p.color0,pixels,snapshot,texBytes,palBytes,false,bytes,stride,psm,nullptr,false,mS,mT,W,H};
  TextureRange(p.textureOffset,texBytes,false,entry->snapshot,nullptr);
  if(palBytes)TextureRange(s_texPlttBase,palBytes,true,entry->snapshot+texBytes,nullptr);
  cacheBytes+=totalBytes;
  u8*src=entry->snapshot;u16*pal=palBytes?(u16*)(entry->snapshot+texBytes):nullptr;u8*const image=(u8*)entry->pixels;
  /* Mirrored entries decode at the DS size into a scratch image, then get composed below. */
  u8*out=image;
  if(mS||mT){out=scratch;
   if(!out){printf("[TEXTURE] mirror buffer failed %ux%u; drawing untextured\n",w,h);cacheBytes-=totalBytes;free(entry->pixels);free(entry->snapshot);*entry={};cacheSize--;sceGuDisable(GU_TEXTURE_2D);return;}}
  if(clutBytes){
   memset(out,0,imageBytes0);unsigned rowBytes=psm==GU_PSM_T4?w/2:w,step=psm==GU_PSM_T4?stride0/2:stride0;
   if(p.textureFormat==GX_TEXFMT_PLTT4){
    static constexpr auto expand=[](){std::array<u16,256>a{};for(unsigned i=0;i<256;i++)a[i]=(i&3)|((i&12)<<2)|((i&48)<<4)|((i&192)<<6);return a;}();
    for(unsigned y=0;y<h;y++){u16*row=(u16*)(out+y*step);for(unsigned x=0;x<w/4;x++)row[x]=expand[src[y*(w/4)+x]];}
   }else for(unsigned y=0;y<h;y++)memcpy(out+y*step,src+y*rowBytes,rowBytes);
   entry->clut=(u32*)(image+imageBytes);
   if(p.textureFormat==GX_TEXFMT_A3I5||p.textureFormat==GX_TEXFMT_A5I3){
    // Preserve the existing decoder alpha expansion exactly, including max248.
    u8 codes[256];for(unsigned i=0;i<256;i++)codes[i]=i;
    if(p.textureFormat==GX_TEXFMT_A3I5)G3SIM_DecodeTexA3I5(codes,pal,(u8*)entry->clut,16,16);
    else G3SIM_DecodeTexA5I3(codes,pal,(u8*)entry->clut,16,16);
   }else for(unsigned i=0;i<clutBytes/4;i++){if(p.textureFormat==GX_TEXFMT_PLTT4&&i>=4){entry->clut[i]=0;continue;}u8 r,g,b;SIM_u16ToRGB(pal[i],&r,&g,&b);entry->clut[i]=r|(g<<8)|(b<<16)|((p.color0&&i==0)?0:0xff000000u);}
  }else if(sh){
   /* 2x2 box downscale without a full-size decode: the decoders are stateless streams, so each source row
      decodes on its own (s=1,t=w) from its byte offset. Two rows at a time, identical output. */
   static u8 rowA[1024*4],rowB[1024*4];
   const unsigned rowBytes=p.textureFormat==GX_TEXFMT_PLTT4?w/4:p.textureFormat==GX_TEXFMT_PLTT16?w/2:p.textureFormat==GX_TEXFMT_DIRECT?w*2:w;
   unsigned ow=w>>1,oh=h>>1;
   if(w>1024){printf("[TEXTURE] downscale width %u unsupported; drawing untextured\n",w);cacheBytes-=totalBytes;free(entry->pixels);free(entry->snapshot);*entry={};cacheSize--;sceGuDisable(GU_TEXTURE_2D);return;}
   auto decodeRow=[&](unsigned y,u8*row){u8*r=src+y*rowBytes;switch(p.textureFormat){
    case GX_TEXFMT_PLTT4:G3SIM_DecodeTex4(r,pal,row,1,w);break;
    case GX_TEXFMT_PLTT16:G3SIM_DecodeTex16(r,pal,row,1,w);break;
    case GX_TEXFMT_PLTT256:G3SIM_DecodeTex256(r,pal,row,1,w);break;
    case GX_TEXFMT_A3I5:G3SIM_DecodeTexA3I5(r,pal,row,1,w);break;
    case GX_TEXFMT_A5I3:G3SIM_DecodeTexA5I3(r,pal,row,1,w);break;
    case GX_TEXFMT_DIRECT:G3SIM_DecodeTexDirect(r,row,1,w);break;
    default:memset(row,0,w*4);break;}};
   for(unsigned y=0;y<oh;y++){
    decodeRow(y*2,rowA);decodeRow(y*2+1,rowB);
    for(unsigned x=0;x<ow;x++){
     const u8*a=rowA+x*8,*b=a+4,*c=rowB+x*8,*d=c+4;u8*o=out+(y*ow+x)*4;
     for(unsigned k=0;k<4;k++)o[k]=u8((unsigned(a[k])+b[k]+c[k]+d[k]+2)>>2);
    }
   }
   static unsigned noted;if(noted<8){noted++;printf("[TEXTURE] downscaled %ux%u -> %ux%u for the GE\n",w,h,ow,oh);}
  }else switch(p.textureFormat){
   case GX_TEXFMT_PLTT4:G3SIM_DecodeTex4(src,pal,out,w,h);break;
   case GX_TEXFMT_PLTT16:G3SIM_DecodeTex16(src,pal,out,w,h);break;
   case GX_TEXFMT_PLTT256:G3SIM_DecodeTex256(src,pal,out,w,h);break;
   case GX_TEXFMT_A3I5:G3SIM_DecodeTexA3I5(src,pal,out,w,h);break;
   case GX_TEXFMT_A5I3:G3SIM_DecodeTexA5I3(src,pal,out,w,h);break;
   case GX_TEXFMT_DIRECT:G3SIM_DecodeTexDirect(src,out,w,h);break;
   default:printf("[TEXTURE] unsupported format%u\n",p.textureFormat);sceKernelExitGame();return;
  }
  if(out!=image){
   /* DS mirrored repeat: texel X in [w,2w) shows source texel 2w-1-X (same for T). */
   if(clutBytes)memset(image,0,imageBytes);
   for(unsigned Y=0;Y<H;Y++){unsigned y=Y<h?Y:2*h-1-Y;
    for(unsigned X=0;X<W;X++){unsigned x=X<w?X:2*w-1-X;
     if(psm==GU_PSM_T4){unsigned v=(out[y*(stride0/2)+(x>>1)]>>((x&1)*4))&15;image[Y*(stride/2)+(X>>1)]|=u8(v<<((X&1)*4));}
     else if(psm==GU_PSM_T8)image[Y*stride+X]=out[y*stride0+x];
     else memcpy(image+(Y*W+X)*4,out+(y*w+x)*4,4);}}
   free(out);out=image;
  }
  // Alpha-zero texels are rejected before color/depth writes. Canonicalize
  // their RGB too: PPSSPP's software CLUT optimization incorrectly tests
  // the whole RGBA bitwise AND when deciding whether alpha can be zero.
  if(entry->clut)for(unsigned i=0;i<clutBytes/4;i++)
   if(!(entry->clut[i]>>24))entry->clut[i]=0;
  {bool pa=false;if(entry->clut){for(unsigned i=0;i<clutBytes/4&&!pa;i++){unsigned a=entry->clut[i]>>24;pa=a&&a!=255;}}
   else {const unsigned n=(W>>sh)*(H>>sh);for(unsigned i=0;i<n&&!pa;i++){unsigned a=entry->pixels[i]>>24;pa=a&&a!=255;}}
   entry->partialAlpha=pa;}
  sceKernelDcacheWritebackRange(entry->pixels,bytes);
  printf("[TEXTURE] decoded format=%u size=%ux%u offset=%u pal=%u bytes=%u\n",p.textureFormat,w,h,p.textureOffset,s_texPlttBase,cacheBytes);printf("[TEXTURE] repeat=%u,%u mirror=%u,%u color0=%u\n",p.repeatS,p.repeatT,p.flipS,p.flipT,p.color0);
 }
 entry->queued=true;g3TexPartialAlpha=entry->partialAlpha;
 sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
 sceGuEnable(GU_TEXTURE_2D);
 if(entry->clut){sceGuClutMode(GU_PSM_8888,0,entry->psm==GU_PSM_T4?15:255,0);sceGuClutLoad(entry->psm==GU_PSM_T4?2:32,entry->clut);}
 sceGuTexMode(entry->psm,0,0,GU_FALSE);{unsigned sh=TexShift(entry->w,entry->h);sceGuTexImage(0,entry->texW>>sh,entry->texH>>sh,entry->stride>>sh,entry->pixels);}sceGuTexScale(entry->mirS?0.5f:1.0f,entry->mirT?0.5f:1.0f);sceGuTexOffset(0.0f,0.0f);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_NEAREST,GU_NEAREST);sceGuTexWrap(p.repeatS?GU_REPEAT:GU_CLAMP,p.repeatT?GU_REPEAT:GU_CLAMP);
}
