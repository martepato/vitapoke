#include <pspkernel.h>
#include <pspgu.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <simulator/g3_draw.h>
#include <simulator/g3_handler.h>
extern "C" void* VitaNativeGUGetMemory(unsigned);
extern "C" void VitaNativeGUTextureFence();
#include "g3_vtx.h"
static GuVertex vertices[6144] __attribute__((aligned(16)));
GuVertex *g3ChunkPtr=vertices;unsigned g3ChunkLeft=6144;
extern "C" void G3SIM_ChunkAlloc(){printf("[NATIVE-RENDER] vertex overflow\n");sceKernelExitGame();std::abort();}
static bool rawMode=false;static unsigned rawMatrixGen;static ScePspFMatrix4 rawClip;
/* Display-list geometry cache support (frontend.cpp; perf port from native-stack-render). While a
   list is being recorded the vertices it appends are the last g3RecCount entries of the chunk; any
   flush in between (matrix change, CPU-path polygon, texture change) invalidates the recording. */
unsigned g3RecCount=0;static bool recActive=false,recBroken=false;
static void ResetVertices(){if(recActive&&g3RecCount)recBroken=true;g3ChunkPtr=vertices;g3ChunkLeft=6144;}
extern "C" void G3ListRecordBegin(){recActive=true;recBroken=false;g3RecCount=0;}
extern "C" int G3ListRecordEnd(unsigned*count,const GuVertex**verts){recActive=false;*count=g3RecCount;*verts=g3ChunkPtr-g3RecCount;return !recBroken;}
extern "C" void G3ListReplay(const GuVertex*v,unsigned n){if(n>g3ChunkLeft)G3SIM_ChunkAlloc();memcpy(g3ChunkPtr,v,n*sizeof(GuVertex));g3ChunkPtr+=n;g3ChunkLeft-=n;}
/* Direct draw from cache memory (16-byte aligned, written back once when stored): pending chunk
   vertices are flushed first so GE order is unchanged. */
static void DrawVertices(const GuVertex*v,unsigned count,bool copy);
extern "C" void G3ListDrawDirect(const GuVertex*v,unsigned n){G3SIM_FlushArray();DrawVertices(v,n,false);}
/* Cache memory the GE may still be reading is freed only after the next sceGuSync. */
static void*pendingFree[256];static unsigned pendingFreeN;
extern "C" void G3ListDeferFree(void*p){if(!p)return;if(pendingFreeN==256)VitaNativeGUTextureFence();pendingFree[pendingFreeN++]=p;}
#ifdef OPT_GE_ASYNC
static unsigned pendingFreeHigh;extern "C" unsigned G3ListPendingFreeHigh(){unsigned v=pendingFreeHigh;pendingFreeHigh=0;return v;}
extern "C" void G3ListPendingFreeDrain(){if(pendingFreeN>pendingFreeHigh)pendingFreeHigh=pendingFreeN;for(unsigned i=0;i<pendingFreeN;i++)free(pendingFree[i]);pendingFreeN=0;}
#else
extern "C" void G3ListPendingFreeDrain(){for(unsigned i=0;i<pendingFreeN;i++)free(pendingFree[i]);pendingFreeN=0;}
#endif
static bool cullEnabled=false,cullAll=false;static int cullFront=GU_CW;
extern "C" {
u8 s_SIM_useWBuffer=0;
void SIM_u16ToRGB(u16 c,u8*r,u8*g,u8*b){unsigned x=c&31,y=(c>>5)&31,z=(c>>10)&31;*r=(x<<3)|(x>>2);*g=(y<<3)|(y>>2);*b=(z<<3)|(z>>2);}
static void APIENTRY NoDebug(GLenum,GLenum,GLuint,GLenum,GLsizei,const GLchar*){}
static void APIENTRY GuEnable(GLenum c){if(c==GL_CULL_FACE){cullEnabled=true;sceGuEnable(GU_CULL_FACE);}else if(c==GL_DEPTH_TEST)sceGuEnable(GU_DEPTH_TEST);}
static void APIENTRY GuDisable(GLenum c){if(c==GL_CULL_FACE){cullEnabled=false;sceGuDisable(GU_CULL_FACE);}else if(c==GL_DEPTH_TEST)sceGuDisable(GU_DEPTH_TEST);}
static void APIENTRY GuCull(GLenum c){cullAll=c==GL_FRONT_AND_BACK;cullFront=c==GL_FRONT?GU_CCW:GU_CW;sceGuFrontFace(cullFront);}
static int g3DepthFunc=GU_LEQUAL;
static void APIENTRY GuDepth(GLenum c){g3DepthFunc=c==GL_LEQUAL?GU_LEQUAL:GU_LESS;sceGuDepthFunc(g3DepthFunc);}
PFNGLDEBUGMESSAGEINSERTPROC glad_glDebugMessageInsert=NoDebug;
PFNGLENABLEPROC glad_glEnable=GuEnable;PFNGLDISABLEPROC glad_glDisable=GuDisable;
PFNGLCULLFACEPROC glad_glCullFace=GuCull;PFNGLDEPTHFUNCPROC glad_glDepthFunc=GuDepth;
}
// DS textures wider/taller than the PSP GE limit (512) are cached at half size: halve their UVs.
static inline unsigned TexShift(unsigned w,unsigned h){return (w>512||h>512)?1u:0u;}
static unsigned channel(float f) { if(f<0)f=0;if(f>1)f=1;return unsigned(f*255.0f+0.5f); }
extern "C" void G3SIM_AddVtx(G3SIM_Vertex_t* v) {
 unsigned count=(unsigned)(g3ChunkPtr-vertices);
 if(count==6144) {printf("[NATIVE-RENDER] vertex overflow\n");sceKernelExitGame();return;}



 // libntr's current shader sets W=1; x/y already underwent CPU perspective divide.
 *g3ChunkPtr++={v->s*float(s_texImageParam.textureSSize>>TexShift(s_texImageParam.textureSSize,s_texImageParam.textureTSize)),v->t*float(s_texImageParam.textureTSize>>TexShift(s_texImageParam.textureSSize,s_texImageParam.textureTSize)),channel(v->r)|(channel(v->g)<<8)|(channel(v->b)<<16)|(channel(v->a)<<24),
                   128.0f+v->x*128.0f,96.0f-v->y*96.0f,v->z*32767.0f+32768.0f};g3ChunkLeft--;
}
#include "texture_cache.h"

#ifdef G3_STREAM_HASH
static unsigned streamHash=2166136261u,streamVertices=0;
static void TraceVertices(const GuVertex*p,unsigned count){const unsigned*w=(const unsigned*)p;for(unsigned i=0;i<count*6;i++)streamHash=(streamHash^w[i])*16777619u;streamVertices+=count;}
extern "C" void VitaNativeVertexTrace(unsigned frame){unsigned aux=2166136261u;const unsigned*w=(const unsigned*)s_g3PolygonVerts;for(unsigned i=0;i<sizeof(s_g3PolygonVerts)/4;i++)aux=(aux^w[i])*16777619u;printf("[VERTEX-TRACE] frame=%u vertices=%u hash=%08x aux=%08x\n",frame,streamVertices,streamHash,aux);}
#else
static void TraceVertices(const GuVertex*,unsigned){}
#endif

extern "C" unsigned g3DrawUs=0,g3DrawN=0;
/* DS rendering order: all opaque polygons of a frame are drawn first, translucent ones after, in
   submission order, and translucent polygons do not write depth unless polygon attribute bit 11
   asks for it. Drawing in submission order instead let a translucent prop submitted before the
   ground blend against the black clear and then occlude the ground: solid black shade under
   signs and gates. Translucent batches are parked here with the GE state they need and drawn
   by G3SIM_FlushDeferred() at the end of the 3D frame (Present). */
extern "C" u8 s_SIM_transDepthWrite;
struct DeferredBatch{const GuVertex*v;unsigned count;G3SIM_TexImageParam_t tex;u32 pltt;ScePspFMatrix4 clip;int depthFunc,front;bool cullOn,cullAll,raw,depthWrite;};
#define DEFERRED_MAX 512
static DeferredBatch deferred[DEFERRED_MAX];static unsigned deferredN,deferredHigh;static bool replaying,batchDepthWrite;
static void DrawVertices(const GuVertex*v,unsigned count,bool copy);
extern "C" void G3SIM_FlushDeferred(){
 if(!deferredN)return;
 G3SIM_TexImageParam_t tex=s_texImageParam;u32 pltt=s_texPlttBase;bool cOn=cullEnabled,cAll=cullAll,raw=rawMode;int front=cullFront,df=g3DepthFunc;
 replaying=true;
 for(unsigned i=0;i<deferredN;i++){DeferredBatch&d=deferred[i];
  s_texImageParam=d.tex;s_texPlttBase=d.pltt;cullEnabled=d.cullOn;cullAll=d.cullAll;cullFront=d.front;
  if(d.cullOn)sceGuEnable(GU_CULL_FACE);else sceGuDisable(GU_CULL_FACE);sceGuFrontFace(d.front);
  g3DepthFunc=d.depthFunc;sceGuDepthFunc(d.depthFunc);
  if(d.raw){ScePspFMatrix4 id;float*f=(float*)&id;for(int k=0;k<16;k++)f[k]=(k%5==0)?1.0f:0.0f;sceGuEnable(GU_CLIP_PLANES);sceGuSetMatrix(GU_PROJECTION,&d.clip);sceGuSetMatrix(GU_VIEW,&id);sceGuSetMatrix(GU_MODEL,&id);}
  rawMode=d.raw;batchDepthWrite=d.depthWrite;
  DrawVertices(d.v,d.count,false);}
 replaying=false;if(deferredN>deferredHigh)deferredHigh=deferredN;deferredN=0;
 s_texImageParam=tex;s_texPlttBase=pltt;cullEnabled=cOn;cullAll=cAll;cullFront=front;g3DepthFunc=df;rawMode=raw;rawMatrixGen=0;
 if(cOn)sceGuEnable(GU_CULL_FACE);else sceGuDisable(GU_CULL_FACE);sceGuFrontFace(front);sceGuDepthFunc(df);
}
extern "C" unsigned G3SIM_DeferredHighWater(){unsigned h=deferredHigh;deferredHigh=0;return h;}
static void DrawVertices(const GuVertex*v,unsigned count,bool copy){
 if(!count||(cullEnabled&&cullAll))return;
#ifdef VITAPOKE_G3_STATS
 unsigned t0=sceKernelGetSystemTimeLow();
#endif
 BindTexture();TraceVertices(v,count);
 const GuVertex*submitted=v;
 if(copy){GuVertex*m=(GuVertex*)VitaNativeGUGetMemory(sizeof(GuVertex)*count);memcpy(m,v,sizeof(GuVertex)*count);submitted=m;}
 sceGuShadeModel(GU_SMOOTH);
 const int vtype=GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|(rawMode?GU_TRANSFORM_3D:GU_TRANSFORM_2D);
 /* Alpha-3D fix (ported from Platinum native-stack-render, 2026-09-13). The 3D buffer's alpha
    channel is the PSP stencil. Opaque batches stamp 255 (unchanged). Translucent batches (polygon
    alpha < 31, or texels with partial alpha) must leave DS-style coverage there instead, otherwise
    the 2D compositor pastes them as opaque colour blended against the black clear. Colour pass
    keeps the stencil; then 8 stencil-only passes quantise fragment alpha and keep the maximum
    (stencil GREATER + REPLACE) = DS alpha max(src,dst). Colour is premultiplied; Draw3DLayer
    composites with ONE. Runs inside DrawVertices, so display-list replays (G3ListDrawDirect)
    get the same passes as recorded draws. */
 const bool translucent=((v[0].color>>24)!=255)||g3TexPartialAlpha;
 if(translucent&&!replaying&&deferredN<DEFERRED_MAX){
  GuVertex*m=(GuVertex*)VitaNativeGUGetMemory(sizeof(GuVertex)*count);memcpy(m,v,sizeof(GuVertex)*count);
  DeferredBatch&d=deferred[deferredN++];d.v=m;d.count=count;d.tex=s_texImageParam;d.pltt=s_texPlttBase;d.clip=rawClip;d.depthFunc=g3DepthFunc;d.front=cullFront;
  d.cullOn=cullEnabled;d.cullAll=cullAll;d.raw=rawMode;d.depthWrite=s_SIM_transDepthWrite!=0;return;}
 const bool depthWrite=replaying?batchDepthWrite:(s_SIM_transDepthWrite!=0);
 if(!translucent){sceGuDrawArray(GU_TRIANGLES,vtype,count,nullptr,submitted);}
 else{
  sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);if(!depthWrite)sceGuDepthMask(GU_TRUE);
  sceGuDrawArray(GU_TRIANGLES,vtype,count,nullptr,submitted);
  sceGuPixelMask(0x00FFFFFF);sceGuDepthMask(GU_TRUE);sceGuDepthFunc(GU_LEQUAL);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_REPLACE);
  for(unsigned k=1;k<=8;k++){unsigned ref=k*32;if(ref>255)ref=255;sceGuAlphaFunc(GU_GREATER,(k-1)*32+15,0xFF);sceGuStencilFunc(GU_GREATER,ref,0xFF);sceGuDrawArray(GU_TRIANGLES,vtype,count,nullptr,submitted);}
  sceGuPixelMask(0);sceGuDepthMask(GU_FALSE);sceGuDepthFunc(g3DepthFunc);sceGuAlphaFunc(GU_GREATER,0,0xff);sceGuStencilFunc(GU_ALWAYS,255,255);
 }
#ifdef VITAPOKE_G3_STATS
 g3DrawUs+=sceKernelGetSystemTimeLow()-t0;g3DrawN++;
#endif
}
extern "C" void G3SIM_DrawArray() {DrawVertices(vertices,(unsigned)(g3ChunkPtr-vertices),true);}
extern "C" void G3SIM_DrawCleanUp() {ResetVertices();rawMatrixGen=0;deferredN=0;}
extern "C" void G3SIM_FlushArray() {G3SIM_DrawArray();ResetVertices();}

extern "C" void VitaNativeG3Release(){for(unsigned i=0;i<cacheSize;i++){free(cache[i].pixels);free(cache[i].snapshot);}cacheSize=cacheBytes=0;ResetVertices();}
extern "C" void VitaNativeG3TrimCache(){while(cacheSize)EvictOne();}

extern "C" void VitaNativeRawMatrix(const s32*m,unsigned gen){
 if(rawMatrixGen==gen&&rawMode)return;
 G3SIM_FlushArray();rawMode=true;rawMatrixGen=gen;sceGuEnable(GU_CLIP_PLANES);
 ScePspFMatrix4 clip,identity;float*c=(float*)&clip,*id=(float*)&identity;for(int i=0;i<16;i++){c[i]=m[i]/4096.0f;id[i]=(i%5==0)?1.0f:0.0f;}
 rawClip=clip;sceGuSetMatrix(GU_PROJECTION,&clip);sceGuSetMatrix(GU_VIEW,&identity);sceGuSetMatrix(GU_MODEL,&identity);
}

extern "C" void VitaNativeRawMode(int value){if(rawMode!=(bool)value){G3SIM_FlushArray();rawMode=value;}}
