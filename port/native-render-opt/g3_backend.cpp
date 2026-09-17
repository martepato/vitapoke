#include <pspkernel.h>
#include <pspgu.h>
#include <cstdio>
#include <cstring>
#include <simulator/g3_draw.h>
#include <simulator/g3_handler.h>
struct GuVertex {float u,v;unsigned color;float x,y,z;};
static GuVertex vertices[6144] __attribute__((aligned(16)));static unsigned count;
extern "C" {
u8 s_SIM_useWBuffer=0;
void SIM_u16ToRGB(u16 c,u8*r,u8*g,u8*b){unsigned x=c&31,y=(c>>5)&31,z=(c>>10)&31;*r=(x<<3)|(x>>2);*g=(y<<3)|(y>>2);*b=(z<<3)|(z>>2);}
static void APIENTRY NoDebug(GLenum,GLenum,GLuint,GLenum,GLsizei,const GLchar*){}
static void APIENTRY GuEnable(GLenum c){if(c==GL_CULL_FACE)sceGuEnable(GU_CULL_FACE);else if(c==GL_DEPTH_TEST)sceGuEnable(GU_DEPTH_TEST);}
static void APIENTRY GuDisable(GLenum c){if(c==GL_CULL_FACE)sceGuDisable(GU_CULL_FACE);else if(c==GL_DEPTH_TEST)sceGuDisable(GU_DEPTH_TEST);}
static void APIENTRY GuCull(GLenum c){sceGuFrontFace(c==GL_FRONT?GU_CW:GU_CCW);}
static void APIENTRY GuDepth(GLenum c){sceGuDepthFunc(c==GL_LEQUAL?GU_LEQUAL:GU_LESS);}
PFNGLDEBUGMESSAGEINSERTPROC glad_glDebugMessageInsert=NoDebug;
PFNGLENABLEPROC glad_glEnable=GuEnable;PFNGLDISABLEPROC glad_glDisable=GuDisable;
PFNGLCULLFACEPROC glad_glCullFace=GuCull;PFNGLDEPTHFUNCPROC glad_glDepthFunc=GuDepth;
}
static unsigned channel(float f) { if(f<0)f=0;if(f>1)f=1;return unsigned(f*255.0f+0.5f); }
extern "C" void G3SIM_AddVtx(G3SIM_Vertex_t* v) {
 if(count==6144) {printf("[NATIVE-RENDER] vertex overflow\n");sceKernelExitGame();return;}



 // libntr's current shader sets W=1; x/y already underwent CPU perspective divide.
 vertices[count++]={v->s*s_texImageParam.textureSSize,v->t*s_texImageParam.textureTSize,channel(v->r)|(channel(v->g)<<8)|(channel(v->b)<<16)|(channel(v->a)<<24),
                   128.0f+v->x*128.0f,96.0f-v->y*96.0f,v->z*32767.0f+32768.0f};
}
#include "texture_cache.h"
extern "C" void G3SIM_DrawArray() {
 if(!count)return;BindTexture();
 GuVertex *submitted=(GuVertex*)sceGuGetMemory(sizeof(GuVertex)*count);
 memcpy(submitted,vertices,sizeof(GuVertex)*count);
 sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,count,nullptr,submitted);
}
extern "C" void G3SIM_DrawCleanUp() {count=0;}
extern "C" void G3SIM_FlushArray() {if(count)G3SIM_DrawArray();count=0;}

extern "C" void VitaNativeG3Release(){for(unsigned i=0;i<cacheSize;i++){free(cache[i].pixels);free(cache[i].snapshot);}cacheSize=cacheBytes=0;count=0;}
