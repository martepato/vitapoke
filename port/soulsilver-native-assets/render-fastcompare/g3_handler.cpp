#include "nitro/fx/fx.h"
#include <pthread.h>
#include <SDL2/SDL.h>

#ifdef SDK_BUILD_NX
#include <glad/glad.h>
#else
#include <simulator/glad/glad.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include <nitro/gx/g3.h>
#include <nitro/hw/X86/ioreg.h>
#include <nitro/hw/X86/mmap_shared.h>

#include <simulator/drawmsg.h>
#include <simulator/queue.h>
#include <simulator/g3_handler.h>
#include <simulator/g3_draw.h>
#include <simulator/sim.h>
#include "g3_vtx.h"

#ifdef SDK_TRACY_ENABLE
#include "tracy/TracyC.h"
#endif

//Constants
const G3SIM_Matrix_t G3SIM_IdentityMatrix = 
    {
        .nums = {
            { FX32_ONE, 0,        0,        0        },
            { 0,        FX32_ONE, 0,        0        },
            { 0,        0,        FX32_ONE, 0        },
            { 0,        0,        0,        FX32_ONE },
        },
    };


GXMtxMode s_curMtxMode = GX_MTXMODE_PROJECTION;
GXBegin s_primType = GX_BEGIN_TRIANGLES;

G3SIM_MatrixStack_t s_projectionStack = {0};
G3SIM_MatrixStack_t s_positionStack = {0};
G3SIM_MatrixStack_t s_vectorStack = {0};
G3SIM_MatrixStack_t s_textureStack = {0};

static G3SIM_Matrix_t currentProjectionMatrix = {0};
static G3SIM_Matrix_t currentPositionMatrix = {0};
static G3SIM_Matrix_t currentVectorMatrix = {0};
static G3SIM_Matrix_t currentTextureMatrix = {0};

static G3SIM_Matrix_t currentClipMatrix = {0};

static u8 s_G3LightColor [4][3];
static float s_G3LightVector[4][3];

u8 s_G3numInPoly = 0;
u8 s_G3drawPoly = 0;
u32 s_G3triStripTriNum = 0;
u32 s_G3quadStripTriNum = 0;

GLfloat s_g3NextTexCoordS;
GLfloat s_g3NextTexCoordT;

G3SIM_FxVtx_t s_g3PolygonVerts[4];

u8 s_g3CurColor[3] = {0};
u8 s_g3DiffuseColor[3] = {0};
u8 s_g3AmbientColor[3] = {0};
u8 s_g3SpecularColor[3] = {0};
u8 s_g3EmissionColor[3] = {0};
G3SIM_PolygonAttr_t s_curPolygonAttr;
extern "C" u8 s_SIM_transDepthWrite;u8 s_SIM_transDepthWrite;

static u8 s_swapBuffersCalledThisFrame;
static u32 s_lightGen;

G3SIM_TexImageParam_t s_texImageParam;
u32 s_texPlttBase;

extern u8 s_SIM_useWBuffer;

static void CommonDebugCommandHandling(draw_command_type_t aCommand);

static float GetVtxPosition(G3SIM_Vertex_t * vtx, int comp)
{
    switch(comp){
        case 0:
            return vtx->x;
        case 1:
            return vtx->y;
        case 2:
            return vtx->z;
        case 3:
            return vtx->w;
        default:
            return 0.0f;
    }
}


void ClipSegment(fx32* outbuf, fx32 * vin, fx32 * vout, int comp, s32 plane, BOOL attribs)
{
    s64 factor_num = (s32)vin[3] - (plane * (s32)vin[comp]);
    s32 factor_den = factor_num - ((s32)vout[3] - (plane*(s32)vout[comp]));

#define INTERPOLATE(var)  { outbuf[var] = (vin[var] + ((vout[var] - vin[var]) * factor_num) / factor_den); }

    if (comp != 0) INTERPOLATE(0);
    if (comp != 1) INTERPOLATE(1);
    if (comp != 2) INTERPOLATE(2);
    INTERPOLATE(3);
    outbuf[comp] = plane*outbuf[3];

    //if (attribs)
    //{
    //    INTERPOLATE(Color[0]);
    //    INTERPOLATE(Color[1]);
    //    INTERPOLATE(Color[2]);
//
    //    INTERPOLATE(TexCoords[0]);
    //    INTERPOLATE(TexCoords[1]);
    //}

    //outbuf->Clipped = true;

#undef INTERPOLATE
}



static int ClipAgainstPlane(fx32 verts[][4], int nVerts, int clipStart, int comp, BOOL attribs)
{
    fx32 temp[10][4];
    int prev, next;
    int c = clipStart;

    if (clipStart == 2)
    {
        temp[0][0] = verts[0][0]; temp[0][1] = verts[0][1]; temp[0][2] = verts[0][2]; temp[0][3] = verts[0][3];
        temp[1][0] = verts[1][0]; temp[1][1] = verts[1][1]; temp[1][2] = verts[1][2]; temp[1][3] = verts[1][3];
    }

    for(int i = clipStart; i < nVerts; i++) {
        prev = i-1; if (prev < 0) prev = nVerts-1;
        next = i+1; if (next >= nVerts) next = 0;

        fx32 vtx[4];
        vtx[0] = verts[i][0]; vtx[1] = verts[i][1]; vtx[2] = verts[i][2]; vtx[3] = verts[i][3];
        if(vtx[comp] > vtx[3]) {
            //if ((comp == 2) && (!(gpu.CurPolygonAttr & (1<<12)))) return 0;  //I think this is the W buffering thing

            fx32 * vprev = verts[prev];
            if(vprev[comp] <= vprev[3]) {
                ClipSegment((fx32*)&temp[c], &vtx[0], vprev, comp, 1, attribs);
                c++;
            }

            fx32 * vnext = verts[next];
            if(vnext[comp] <= vnext[3]) {
                ClipSegment((fx32*)&temp[c], &vtx[0], vnext, comp, 1, attribs);
                c++;
            }
        } else {
            memcpy(&temp[c++], vtx, sizeof(fx32)*4);
        }
    }

    nVerts = c;
    c = clipStart;
    for(int i=clipStart; i < nVerts; i++) {
        prev = i-1; if (prev < 0) prev = nVerts-1;
        next = i+1; if (next >= nVerts) next = 0;

        fx32 vtx[4];
        vtx[0] = temp[i][0]; vtx[1] = temp[i][1]; vtx[2] = temp[i][2]; vtx[3] = temp[i][3];
        if(vtx[comp] < -vtx[3]) {
            fx32 * vprev = temp[prev];
            if(vprev[comp] >= -vprev[3]) {
                ClipSegment((fx32*)&verts[c], &vtx[0], vprev, comp, -1, attribs);
                c++;
            }

            fx32 * vnext = temp[next];
            if(vnext[comp] >= -vnext[3]) {
                ClipSegment((fx32*)&verts[c], &vtx[0], vnext, comp, -1, attribs);
                c++;
            }
        } else {
            memcpy(&verts[c++], vtx, sizeof(fx32)*4);
        }
    }

    return c;
}

int G3SIM_ClipPolygon(fx32 verts[][4], int nVerts, int clipStart, BOOL attribs)
{
    // Z clipping
    nVerts = ClipAgainstPlane(verts, nVerts, clipStart, 2, attribs);

    // Y clipping
    nVerts = ClipAgainstPlane(verts, nVerts, clipStart, 1, attribs);

    // X clipping
    nVerts = ClipAgainstPlane(verts, nVerts, clipStart, 0, attribs);
    return nVerts;
}

static G3SIM_Matrix_t * getCurrentMatrix()
{
    switch( s_curMtxMode )
    {
        case GX_MTXMODE_PROJECTION:
            return &currentProjectionMatrix;
            break;
        case GX_MTXMODE_POSITION:
            return &currentPositionMatrix;
            break;
        case GX_MTXMODE_POSITION_VECTOR:
            return &currentVectorMatrix;
            break;
        case GX_MTXMODE_TEXTURE:
            return &currentTextureMatrix;
            break;
        default:
            return NULL;
    }
}

//Return a pointer to the top matrix from the current matrix stack
static G3SIM_Matrix_t * getTopMatrixFromCurrentStack()
{
    switch( s_curMtxMode )
    {
        case GX_MTXMODE_PROJECTION:
            return &s_projectionStack.mtxs[0];
            break;
        case GX_MTXMODE_POSITION:
            return &s_positionStack.mtxs[s_positionStack.stackPtr];
            break;
        case GX_MTXMODE_POSITION_VECTOR:
            return &s_vectorStack.mtxs[s_vectorStack.stackPtr];
            break;
        case GX_MTXMODE_TEXTURE:
            return &s_textureStack.mtxs[0];
            break;
        default:
            return NULL;
    }
}

static G3SIM_Matrix_t * getPositionMatrix()
{
    return &s_positionStack.mtxs[s_positionStack.stackPtr];
}

static G3SIM_Matrix_t * getVectorMatrix()
{
    return &s_vectorStack.mtxs[s_vectorStack.stackPtr];
}

//Get a pointer to the current matrix stack structure
static G3SIM_MatrixStack_t * getCurrentMatrixStack()
{
    switch( s_curMtxMode )
    {
        case GX_MTXMODE_PROJECTION:
            return &s_projectionStack;
            break;
        case GX_MTXMODE_POSITION:
            return &s_positionStack;
            break;
        case GX_MTXMODE_POSITION_VECTOR:
            return &s_vectorStack;
            break;
        case GX_MTXMODE_TEXTURE:
            return &s_textureStack;
            break;
        default:
            return NULL;
    }
}

static G3SIM_MatrixStack_t * getPositionMatrixStack()
{
    return &s_positionStack;
}

static G3SIM_MatrixStack_t * getVectorMatrixStack()
{
    return &s_vectorStack;
}

//Calculate the clip matrix
//Multiply the PositionMatrix * ProjectionMatrix
#ifndef VITAPOKE_GE_TRANSFORM
#define VITAPOKE_GE_TRANSFORM 1
#endif
static unsigned nativeMatrixGen=1;
static void NativeFinishPendingVertices();
static bool NativeHasPendingVertices();
#ifdef OPT_MATH_LAZYFIN
static void NativeDeferPendingVertices();
#endif
static void computeClipMatrixNow();
/* Perf port from native-stack-render (2026-09-13): the clip matrix (position x projection)
   used to be recomputed on every matrix command; it is now computed on first use (vertex,
   box/position test, GE matrix upload, G3X_GetClipMtx via the app's --wrap). Vertices still
   pending from before a matrix change are finished with the old matrix first, as before. */
#ifdef OPT_MATH_PROF
#include <pspkernel.h>
unsigned omp[16],ompUs[16];
#define OMP_T0() unsigned ompT=sceKernelGetSystemTimeLow()
#define OMP_T1(i) (ompUs[i]+=sceKernelGetSystemTimeLow()-ompT,omp[i]++)
#define OMP_N(i) (omp[i]++)
#else
#define OMP_T0() ((void)0)
#define OMP_T1(i) ((void)0)
#define OMP_N(i) ((void)0)
#endif
/* OPT_MATH_*: exact-arithmetic fast paths. OPT_MATH_ORACLE runs the previous implementation
   on a copy of the same inputs and counts any bit difference (test builds only). */
#ifdef OPT_MATH_ORACLE
unsigned omChecks[8],omMis[8];   /* 0 clip,1 scale,2 mul43,3 box(determined),4 box fallback,5 corners */
struct OMState{G3SIM_Matrix_t proj,pos,vec,tex;};
static void OMSave(OMState&o){o.proj=currentProjectionMatrix;o.pos=currentPositionMatrix;o.vec=currentVectorMatrix;o.tex=currentTextureMatrix;}
static G3SIM_Matrix_t*OMCur(OMState&o){switch(s_curMtxMode){case GX_MTXMODE_PROJECTION:return &o.proj;case GX_MTXMODE_POSITION:return &o.pos;case GX_MTXMODE_POSITION_VECTOR:return &o.vec;case GX_MTXMODE_TEXTURE:return &o.tex;default:return NULL;}}
static int OMCmp(OMState&o){return memcmp(&o.proj,&currentProjectionMatrix,sizeof o.proj)|memcmp(&o.pos,&currentPositionMatrix,sizeof o.pos)|memcmp(&o.vec,&currentVectorMatrix,sizeof o.vec)|memcmp(&o.tex,&currentTextureMatrix,sizeof o.tex);}
#define OM_CHECK(i,bad) (omChecks[i]++,(bad)?(omMis[i]++,printf("[OPT-ORACLE] MISMATCH kind=%d\n",i)):0)
#endif
static bool clipDirty=false;
static inline void EnsureClipMatrix(){if(clipDirty){clipDirty=false;computeClipMatrixNow();OMP_N(1);}}
extern "C" void G3SIM_EnsureClipMatrix(){EnsureClipMatrix();}
static void calculateClipMatrix()
{
#ifdef OPT_MATH_LAZYFIN
    if(NativeHasPendingVertices()){OMP_T0();EnsureClipMatrix();NativeDeferPendingVertices();OMP_T1(0);}
#else
    if(NativeHasPendingVertices()){OMP_T0();EnsureClipMatrix();NativeFinishPendingVertices();OMP_T1(0);}
#endif
    clipDirty=true;
    ++nativeMatrixGen;
}
static void computeClipMatrixNow()
{
#ifdef OPT_MATH_CLIP
    /* FX_Mul(p,0)==0 and FX_Mul(4096,m)==m exactly, so zero projection entries and zero/one
       position entries contribute exactly what the full 64-product loop added. Sums wrap in
       u32 like the original's two's-complement accumulation. */
    {
        const G3SIM_Matrix_t&P=currentProjectionMatrix,&M=currentPositionMatrix;
        for(int j=0;j<4;j++){
            int ks[4],nk=0;for(int k=0;k<4;k++)if(P.nums[k][j])ks[nk++]=k;
            for(int i=0;i<4;i++){
                u32 sum=0;
                for(int t=0;t<nk;t++){int k=ks[t];fx32 a=M.nums[i][k];if(!a)continue;fx32 b=P.nums[k][j];
                    sum+=(a==FX32_ONE)?(u32)b:(u32)(((s64)a*b+0x800)>>12);}
                currentClipMatrix.nums[i][j]=(fx32)sum;
            }
        }
#ifdef OPT_MATH_ORACLE
        G3SIM_Matrix_t ref;
        for(int i=0;i<4;i++)for(int j=0;j<4;j++){fx32 cs=0;for(int k=0;k<4;k++)cs=(fx32)((u32)cs+(u32)FX_Mul(currentPositionMatrix.nums[i][k],currentProjectionMatrix.nums[k][j]));ref.nums[i][j]=cs;}
        OM_CHECK(0,memcmp(&ref,&currentClipMatrix,sizeof ref));
#endif
    }
    if(0)
#endif
    {
    fx32 curSum = 0;
    //A * B
    for( int i = 0; i < 4; i++ ) //Row of A
    {
        for( int j = 0; j < 4; j++ )
        {
            
            //Get the dot product of curRow * curCol
            curSum = 0;
            for(int k=0; k < 4; k++)
            {
                curSum = (fx32)((u32)curSum + (u32)FX_Mul(currentPositionMatrix.nums[i][k], currentProjectionMatrix.nums[k][j]));
            }
            currentClipMatrix.nums[i][j] = curSum;
        }
    }
    }

    //Update reg_G3X_CLIPMTX_RESULT
    MtxFx44 fxMtx;
    fxMtx._00 = currentClipMatrix.nums[0][0];
    fxMtx._01 = currentClipMatrix.nums[0][1];
    fxMtx._02 = currentClipMatrix.nums[0][2];
    fxMtx._03 = currentClipMatrix.nums[0][3];
    fxMtx._10 = currentClipMatrix.nums[1][0];
    fxMtx._11 = currentClipMatrix.nums[1][1];
    fxMtx._12 = currentClipMatrix.nums[1][2];
    fxMtx._13 = currentClipMatrix.nums[1][3];
    fxMtx._20 = currentClipMatrix.nums[2][0];
    fxMtx._21 = currentClipMatrix.nums[2][1];
    fxMtx._22 = currentClipMatrix.nums[2][2];
    fxMtx._23 = currentClipMatrix.nums[2][3];
    fxMtx._30 = currentClipMatrix.nums[3][0];
    fxMtx._31 = currentClipMatrix.nums[3][1];
    fxMtx._32 = currentClipMatrix.nums[3][2];
    fxMtx._33 = currentClipMatrix.nums[3][3];

    memcpy( (void*)&s_reg_G3X_CLIPMTX_RESULT_0, &fxMtx, sizeof( MtxFx44 ) );


    return;
}

static void calculateVtxFxM(const G3SIM_Matrix_t&currentClipMatrix,fx32 * x, fx32 * y, fx32 * z, fx32 * w)
{
    fx32 myW = FX32_ONE;
    fx32 oldVec[4];
    fx32 newVec[4];
    fx32 curSum;

    oldVec[0] = *x;
    oldVec[1] = *y;
    oldVec[2] = *z;
    oldVec[3] = FX32_ONE;

    for( int i=0; i < 4; i++ )
    {
        // Preserve each fixed-point product's rounding and 32-bit wrapping.
        u32 sum = (u32)(((s64)oldVec[0]*currentClipMatrix.nums[0][i]+0x800)>>12);
        sum += (u32)(((s64)oldVec[1]*currentClipMatrix.nums[1][i]+0x800)>>12);
        sum += (u32)(((s64)oldVec[2]*currentClipMatrix.nums[2][i]+0x800)>>12);
        sum += (u32)currentClipMatrix.nums[3][i]; // FX_Mul(4096,m) == m
        newVec[i]=(fx32)sum;
    }
    *x = newVec[0];
    *y = newVec[1];
    *z = newVec[2];
    *w = newVec[3];
}
static void calculateVtxFx(fx32 * x, fx32 * y, fx32 * z, fx32 * w){EnsureClipMatrix();calculateVtxFxM(currentClipMatrix,x,y,z,w);}

static void calculateVtx(float * x, float * y, float * z, float * w)
{
    EnsureClipMatrix();
    float myW = 1.0f;

    float oldVec[4];
    float newVec[4];
    float curSum;

    oldVec[0] = *x;
    oldVec[1] = *y;
    oldVec[2] = *z;
    oldVec[3] = 1.0f;

    for( int i=0; i < 4; i++ )
    {
        curSum = 0.0f;
        for(int j=0; j < 4; j++ )
        {
            curSum = curSum + (oldVec[j] * FX_FX32_TO_F32(currentClipMatrix.nums[j][i]));
        }
        newVec[i] = curSum;
    }
    *x = newVec[0];
    *y = newVec[1];
    *z = newVec[2];
    *w = newVec[3];
    return;
}

//Begin vertices
void G3SIM_Begin(GXBegin type)
{
    s_primType = type;
    s_G3numInPoly = 0;
    s_G3drawPoly = 0;
    s_G3triStripTriNum = 0;
    s_G3quadStripTriNum = 0;

    //printf("GXBegin %d\n", type);

    return;
}

#ifdef OPT_MATH_BOX
/* Per-face outcome of G3SIM_ClipPolygon(face,4,0,FALSE) decided without running the clipper:
   1 = count>0, 0 = count 0, -1 = undetermined (run the real clipper). ClipSegment used to be
   stubbed, which left the undetermined case reading uninitialised vertices: map props whose
   bounding box is larger than the view (long conveyor belts) then vanished at random. A vertex inside all three axes is copied by every pass. A plane whose
   vertices are all beyond +w yields 0 in the first pass; all beyond -w (and none beyond +w)
   yields 0 in the second. Planes before that must keep all four vertices unchanged. */
static int BoxFaceClass(const fx32*v[4]){
    for(int n=0;n<4;n++){const fx32*p=v[n];
        if(!(p[2]>p[3])&&!(p[2]< -p[3])&&!(p[1]>p[3])&&!(p[1]< -p[3])&&!(p[0]>p[3])&&!(p[0]< -p[3]))return 1;}
    static const int axes[3]={2,1,0};
    for(int t=0;t<3;t++){int a=axes[t];int allIn=1,allAbove=1,allBelow=1;
        for(int n=0;n<4;n++){const fx32*p=v[n];bool ab=p[a]>p[3],be=p[a]< -p[3];
            if(ab||be)allIn=0;if(!ab)allAbove=0;if(!be||ab)allBelow=0;}
        if(allAbove||allBelow)return 0;
        if(!allIn)return -1;
    }
    return 1;
}
static void BoxTestClipOrig(fx32 cube[8][4]) __attribute__((noinline));
void G3SIM_BoxTest(s16 x, s16 y, s16 z, s16 width, s16 height, s16 depth)
{
    s16 x1=width,y1=height,z1=depth;x1+=x;y1+=y;z1+=z;
    EnsureClipMatrix();
    const G3SIM_Matrix_t&M=currentClipMatrix;
    /* calculateVtxFx sums four independently rounded products; each coordinate's products
       depend only on that coordinate, so the 8 corners share 24 products instead of 96. */
    u32 px[2][4],py[2][4],pz[2][4];fx32 cx[2]={x,x1},cy[2]={y,y1},cz[2]={z,z1};
    for(int i=0;i<4;i++)for(int b=0;b<2;b++){
        px[b][i]=(u32)(((s64)cx[b]*M.nums[0][i]+0x800)>>12);
        py[b][i]=(u32)(((s64)cy[b]*M.nums[1][i]+0x800)>>12);
        pz[b][i]=(u32)(((s64)cz[b]*M.nums[2][i]+0x800)>>12);}
    static const u8 sel[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,1,1},{0,0,1},{1,0,1},{1,1,1}};
    fx32 cube[8][4];
    for(int c=0;c<8;c++)for(int i=0;i<4;i++)cube[c][i]=(fx32)(px[sel[c][0]][i]+py[sel[c][1]][i]+pz[sel[c][2]][i]+(u32)M.nums[3][i]);
#ifdef OPT_MATH_ORACLE
    {fx32 rc[8][4];s16 xs[2]={x,x1},ys[2]={y,y1},zs[2]={z,z1};
     for(int c=0;c<8;c++){rc[c][0]=xs[sel[c][0]];rc[c][1]=ys[sel[c][1]];rc[c][2]=zs[sel[c][2]];calculateVtxFx(&rc[c][0],&rc[c][1],&rc[c][2],&rc[c][3]);}
     OM_CHECK(5,memcmp(rc,cube,sizeof rc));}
#endif
    static const u8 faces[6][4]={{0,1,2,3},{4,5,6,7},{0,3,4,5},{1,2,7,6},{0,1,6,5},{2,3,4,7}};
    int verdict=0;   /* 0 all faces zero, 1 visible, -1 undetermined */
    for(int f=0;f<6;f++){const fx32*v[4]={cube[faces[f][0]],cube[faces[f][1]],cube[faces[f][2]],cube[faces[f][3]]};
        int r=BoxFaceClass(v);if(r==1){verdict=1;break;}if(r<0)verdict=-1;}
    OMP_N(verdict==1?5:verdict==0?6:7);
    if(verdict<0){
#ifdef OPT_MATH_ORACLE
        omChecks[4]++;
#endif
        BoxTestClipOrig(cube);return;}
#ifdef OPT_MATH_ORACLE
    {u32 save=s_reg_G3X_GXSTAT;BoxTestClipOrig(cube);int refVis=(s_reg_G3X_GXSTAT>>1)&1;s_reg_G3X_GXSTAT=save;OM_CHECK(3,refVis!=verdict);}
#endif
    if(verdict)s_reg_G3X_GXSTAT |= (1<<1);
    else s_reg_G3X_GXSTAT &= 0b11111111111111111111111111111101;
}
static void BoxTestClipOrig(fx32 cube[8][4])
{
    fx32 face[10][4];
    int res = 0;
#else
//Box Test
void G3SIM_BoxTest(s16 x, s16 y, s16 z, s16 width, s16 height, s16 depth)
{
    fx32 cube[8][4];
    fx32 face[10][4];

    s16 x0 = x;
    s16 y0 = y;
    s16 z0 = z;
    s16 x1 = width;
    s16 y1 = height;
    s16 z1 = depth;

    int res = 0;

    x1 += x0;
    y1 += y0;
    z1 += z0;

    cube[0][0] = x0; cube[0][1] = y0; cube[0][2] = z0;
    cube[1][0] = x1; cube[1][1] = y0; cube[1][2] = z0;
    cube[2][0] = x1; cube[2][1] = y1; cube[2][2] = z0;
    cube[3][0] = x0; cube[3][1] = y1; cube[3][2] = z0;
    cube[4][0] = x0; cube[4][1] = y1; cube[4][2] = z1;
    cube[5][0] = x0; cube[5][1] = y0; cube[5][2] = z1;
    cube[6][0] = x1; cube[6][1] = y0; cube[6][2] = z1;
    cube[7][0] = x1; cube[7][1] = y1; cube[7][2] = z1;

    for(int i=0; i < 8; i++) {
        calculateVtxFx(&cube[i][0], &cube[i][1], &cube[i][2], &cube[i][3]);
    }
#endif

    #define SetupFace(_face0,_face1,_face2,_face3) \
        face[0][0] = cube[_face0][0]; face[0][1] = cube[_face0][1]; face[0][2] = cube[_face0][2]; face[0][3] = cube[_face0][3]; \
        face[1][0] = cube[_face1][0]; face[1][1] = cube[_face1][1]; face[1][2] = cube[_face1][2]; face[1][3] = cube[_face1][3]; \
        face[2][0] = cube[_face2][0]; face[2][1] = cube[_face2][1]; face[2][2] = cube[_face2][2]; face[2][3] = cube[_face2][3]; \
        face[3][0] = cube[_face3][0]; face[3][1] = cube[_face3][1]; face[3][2] = cube[_face3][2]; face[3][3] = cube[_face3][3];

    // front face (-Z)
    SetupFace(0, 1, 2, 3);
    res += G3SIM_ClipPolygon(face, 4, 0, FALSE);

    // back face (+Z)
    SetupFace(4, 5, 6, 7);
    res += G3SIM_ClipPolygon(face, 4, 0, FALSE);

    // left face (-X)
    SetupFace(0, 3, 4, 5);
    res += G3SIM_ClipPolygon(face, 4, 0, FALSE);

    // right face (+X)
    SetupFace(1, 2, 7, 6);
    res += G3SIM_ClipPolygon(face, 4, 0, FALSE);

    // bottom face (-Y)
    SetupFace(0, 1, 6, 5);
    res += G3SIM_ClipPolygon(face, 4, 0, FALSE);

    // top face (+Y)
    SetupFace(2, 3, 4, 7);
    res += G3SIM_ClipPolygon(face, 4, 0, FALSE);

    if(res > 0){
        s_reg_G3X_GXSTAT |= (1<<1);
        return;
    }
    
    s_reg_G3X_GXSTAT &= 0b11111111111111111111111111111101;
    return;
}

//Set the color
void G3SIM_Color(u16 color)
{
    u8 r;
    u8 g;
    u8 b;
	r = color & 0b0000000000011111;
    if(r == 31) {
        r = 255;
    } else {
        r = r * 7;
    }
	g = (color >> 5) & 0b0000000000011111;
    if(g == 31) {
        g = 255;
    } else {
        g = g * 7;
    }
	b = (color >> 10) & 0b0000000000011111;
    if(b == 31) {
        b = 255;
    } else {
        b = b * 7;
    }

    //printf("Color: %d %d %d\n", r, g, b);
    s_g3CurColor[0] = r;
    s_g3CurColor[1] = g;
    s_g3CurColor[2] = b;

    return;
}

void G3SIM_DecodeTex4(u8* vramTex, u16* colorAddr, u8* out, u32 s, u32 t){
    u32 texOffset = 0;
    u8 color1;
    u8 color2;
    u8 color3;
    u8 color4;
    u16 palColor;

    u8 r;
    u8 g;
    u8 b;
    for(int i=0; i < s; i++)
    {
        for(int j=0; j < t / 4; j++)
        {
            color1 = (*vramTex) & 0b00000011;
			color2 = ((*vramTex) & 0b00001100) >> 2;
            color3 = ((*vramTex) & 0b00110000) >> 4;
            color4 = ((*vramTex) & 0b11000000) >> 6;

            palColor = colorAddr[color1];
            SIM_u16ToRGB( palColor, &r, &g, &b );
			out[texOffset] = r;
			texOffset++;
			out[texOffset] = g;
			texOffset++;
			out[texOffset] = b;
			texOffset++;
            if(s_texImageParam.color0 == 1 && color1 == 0)
            {
                out[texOffset] = 0;
            }else{
                out[texOffset] = 255;
            }
			texOffset++;

            palColor = colorAddr[color2];
            SIM_u16ToRGB( palColor, &r, &g, &b );
			out[texOffset] = r;
			texOffset++;
			out[texOffset] = g;
			texOffset++;
			out[texOffset] = b;
			texOffset++;
            if(s_texImageParam.color0 == 1 && color2 == 0)
            {
                out[texOffset] = 0;
            }else{
                out[texOffset] = 255;
            }
			texOffset++;

            palColor = colorAddr[color3];
            SIM_u16ToRGB( palColor, &r, &g, &b );
			out[texOffset] = r;
			texOffset++;
			out[texOffset] = g;
			texOffset++;
			out[texOffset] = b;
			texOffset++;
            if(s_texImageParam.color0 == 1 && color3 == 0)
            {
                out[texOffset] = 0;
            }else{
                out[texOffset] = 255;
            }
			texOffset++;

            palColor = colorAddr[color4];
            SIM_u16ToRGB( palColor, &r, &g, &b );
			out[texOffset] = r;
			texOffset++;
			out[texOffset] = g;
			texOffset++;
			out[texOffset] = b;
			texOffset++;
            if(s_texImageParam.color0 == 1 && color4 == 0)
            {
                out[texOffset] = 0;
            }else{
                out[texOffset] = 255;
            }
			texOffset++;

            vramTex++;
        }
    }
}

void G3SIM_DecodeTex16(u8* vramTex, u16* colorAddr, u8* out, u32 s, u32 t){
    //4 bit per pixel (16 colour)
	u32 texOffset = 0;
    if( vramTex == NULL )
    {
        //This probably should not happen
        return;
    }
	for( int i=0; i < s; i++ )
	{
		for( int j=0; j < t / 2; j++ )
		{
			u8 color1 = (*vramTex) & 0b00001111;
			u8 color2 = ((*vramTex) & 0b11110000) >> 4;
			u16 palColor1;
			u16 palColor2;
			palColor1 = colorAddr[color1];
			palColor2 = colorAddr[color2];

			u8 r;
			u8 g;
			u8 b;

			SIM_u16ToRGB( palColor1, &r, &g, &b );
			out[texOffset] = r;
			texOffset++;
			out[texOffset] = g;
			texOffset++;
			out[texOffset] = b;
			texOffset++;
            if(s_texImageParam.color0 == 1 && color1 == 0)
            {
                out[texOffset] = 0;
            }else{
                out[texOffset] = 255;
            }
			texOffset++;

			//Color 2
			SIM_u16ToRGB( palColor2, &r, &g, &b );
			out[texOffset] = r;
			texOffset++;
			out[texOffset] = g;
			texOffset++;
			out[texOffset] = b;
			texOffset++;
            if(s_texImageParam.color0 == 1 && color2 == 0)
            {
                out[texOffset] = 0;
            }else{
                out[texOffset] = 255;
            }
			texOffset++;
			vramTex++;
		}
	}
}

void G3SIM_DecodeTex256(u8* vramTex, u16* colorAddr, u8* out, u32 s, u32 t){
    //4 bit per pixel (16 colour)
	u32 texOffset = 0;
	for( int i=0; i < s; i++ )
	{
		for( int j=0; j < t; j++ )
		{
			u8 color = (*vramTex);
			u16 palColor;
			palColor = colorAddr[color];

			u8 r;
			u8 g;
			u8 b;

			SIM_u16ToRGB( palColor, &r, &g, &b );
			out[texOffset] = r;
			texOffset++;
			out[texOffset] = g;
			texOffset++;
			out[texOffset] = b;
			texOffset++;
            if(s_texImageParam.color0 == 1 && color == 0)
            {
                out[texOffset] = 0;
            }else{
                out[texOffset] = 255;
            }
			texOffset++;
			vramTex++;
		}
	}
}

void G3SIM_DecodeTexA3I5(u8* vramTex, u16* colorAddr, u8* out, u32 s, u32 t)
{
    #ifdef SDK_TRACY_ENABLE
    TracyCZone(ctx, 1);
    #endif
    u32 texOffset = 0;
    u8 colorIdx;
    u8 alpha;

    u8 r;
    u8 g;
    u8 b;

    u16 color;
    for(int i=0; i < s; i++)
    {
        for(int j=0; j < t; j++)
        {
            colorIdx = (*vramTex) & 0b00011111;
            alpha = ((*vramTex) & 0b11100000) >> 5;
            //See GBATek A3I5
            alpha = (alpha*4) + (alpha/2);
            color = colorAddr[colorIdx];
            SIM_u16ToRGB( color, &r, &g, &b );
			out[texOffset] = r;
			texOffset++;
			out[texOffset] = g;
			texOffset++;
			out[texOffset] = b;
			texOffset++;
			out[texOffset] = alpha * 8;
			texOffset++;
            vramTex++;
        }
    }
    #ifdef SDK_TRACY_ENABLE
    TracyCZoneEnd(ctx);
    #endif
}

void G3SIM_DecodeTexA5I3(u8* vramTex, u16* colorAddr, u8* out, u32 s, u32 t)
{
    #ifdef SDK_TRACY_ENABLE
    TracyCZone(ctx, 1);
    #endif
    u32 texOffset = 0;
    u8 colorIdx;
    u8 alpha;

    u8 r;
    u8 g;
    u8 b;

    u16 color;
    for(int i=0; i < s; i++)
    {
        for(int j=0; j < t; j++)
        {
            colorIdx = (*vramTex) & 0b00000111;
            alpha = ((*vramTex) & 0b11111000) >> 3;
            color = colorAddr[colorIdx];
            SIM_u16ToRGB( color, &r, &g, &b );
			out[texOffset] = r;
			texOffset++;
			out[texOffset] = g;
			texOffset++;
			out[texOffset] = b;
			texOffset++;
			out[texOffset] = alpha*8;
			texOffset++;
            vramTex++;
        }
    }
    #ifdef SDK_TRACY_ENABLE
    TracyCZoneEnd(ctx);
    #endif
}

void G3SIM_DecodeTexDirect(u8* vramTex, u8* out, u32 s, u32 t)
{
    #ifdef SDK_TRACY_ENABLE
    TracyCZone(ctx, 1);
    #endif
    u16 * colorAddr = (u16*)vramTex;
    u8 r;
    u8 g;
    u8 b;
    u32 texOffset = 0;
    for(int i=0; i < s; i++)
    {
        for(int j = 0; j < t; j++ )
        {
            u16 color = *colorAddr;
            SIM_u16ToRGB( color, &r, &g, &b );
			out[texOffset] = r;
			texOffset++;
			out[texOffset] = g;
			texOffset++;
			out[texOffset] = b;
			texOffset++;
			out[texOffset] = 255;
			texOffset++;
            colorAddr++;
        }
    }
    #ifdef SDK_TRACY_ENABLE
    TracyCZoneEnd(ctx);
    #endif
}

void G3SIM_DiffAmb(u32 data)
{
    s_lightGen++;
    u8 diffuseRed = (data & 0b11111);
    u8 diffuseGreen = (data & 0b1111100000) >> 5;
    u8 diffuseBlue = (data & 0b111110000000000) >> 10;
    u8 setVtxColor = (data & 0b1000000000000000) >> 15;
    u8 ambientRed = (data & 0b111110000000000000000) >> 16;
    u8 ambientGreen = (data & 0b11111000000000000000000000) >> 21;
    u8 ambientBlue = (data & 0b1111100000000000000000000000000) >> 26;

    if(setVtxColor) {
        s_g3CurColor[0] = diffuseRed << 3;
        s_g3CurColor[1] = diffuseGreen << 3;
        s_g3CurColor[2] = diffuseBlue << 3;
    }
    s_g3DiffuseColor[0] = diffuseRed << 3;
    s_g3DiffuseColor[1] = diffuseGreen << 3;
    s_g3DiffuseColor[2] = diffuseBlue << 3;

    s_g3AmbientColor[0] = ambientRed << 3;
    s_g3AmbientColor[1] = ambientGreen << 3;
    s_g3AmbientColor[2] = ambientBlue << 3;
    return;
}

//Copy the identity matrix to the current matrix
void G3SIM_Identity()
{
    G3SIM_Matrix_t * curMtx = getCurrentMatrix();
    if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR )
    {
        //Also copy to position matrix
        memcpy( &currentPositionMatrix, &G3SIM_IdentityMatrix, sizeof(G3SIM_Matrix_t) );
    }
    memcpy( curMtx, &G3SIM_IdentityMatrix, sizeof(G3SIM_Matrix_t) );

    if( s_curMtxMode != GX_MTXMODE_TEXTURE)
    {
        calculateClipMatrix();
    }
    return;
}

void G3SIM_LightColor(u32 data)
{
    s_lightGen++;
    u8 lightRed = (data & 0b11111);
    u8 lightGreen = (data & 0b1111100000) >> 5;
    u8 lightBlue = (data & 0b111110000000000) >> 10;
    u8 lightNum = (data & 0b11000000000000000000000000000000) >> 30;

    // Should get us close but colors might not be 100% accurate
    s_G3LightColor[lightNum][0] = lightRed << 3;
    s_G3LightColor[lightNum][1] = lightGreen << 3;
    s_G3LightColor[lightNum][2] = lightBlue << 3;
}

static float DecodeSignedFx10(unsigned bits){int v=bits&1023;if(v&512)v-=1024;return v*(1.0f/512.0f);}
void G3SIM_LightVector(u32 data)
{
    s_lightGen++;
    unsigned lightNum=data>>30;
    // LIGHT_VECTOR captures the matrix at command execution, not at NORMAL.
    s32 dir[3];for(int i=0;i<3;i++){dir[i]=(data>>(i*10))&1023;if(dir[i]&512)dir[i]-=1024;}
    for(int j=0;j<3;j++){
        s64 sum=0;for(int i=0;i<3;i++)sum+=(s64)dir[i]*currentVectorMatrix.nums[i][j];
        s_G3LightVector[lightNum][j]=(s16)(sum>>12)*(1.0f/512.0f);
    }
}

//Load Matrix
void G3SIM_MtxLoad43(MtxFx43 * mtx)
{
    G3SIM_Matrix_t * curMtx = getCurrentMatrix();
    fx32 num;
    int curNum = 0;
    for(int i=0; i < 4; i++)
    {
        for(int j=0; j<3; j++)
        {
            curMtx->nums[i][j] = mtx->m[i][j];
            if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR )
            {
                currentPositionMatrix.nums[i][j] = mtx->m[i][j];
            }
        }
        if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR )
        {
            if( i==3 )
            {
                currentPositionMatrix.nums[i][3] = FX32_ONE;
            }
            else
            {
                currentPositionMatrix.nums[i][3] = 0;
            }
            
        }
        if( i==3 )
        {
            curMtx->nums[i][3] = FX32_ONE;
        }
        else
        {
            curMtx->nums[i][3] = 0;
        }
        
    }
    if( s_curMtxMode != GX_MTXMODE_TEXTURE )
    {
        calculateClipMatrix();
    }
    return;
}

void G3SIM_MtxLoad44(MtxFx44 * mtx)
{
    G3SIM_Matrix_t * curMtx = getCurrentMatrix();
    fx32 num;
    int curNum = 0;
    for(int i=0; i < 4; i++)
    {
        for(int j=0; j<4; j++)
        {
            curMtx->nums[i][j] = mtx->m[i][j];
            if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR )
            {
                currentPositionMatrix.nums[i][j] = mtx->m[i][j];
            }
        }
    }
        if( s_curMtxMode != GX_MTXMODE_TEXTURE )
    {
        calculateClipMatrix();
    }
    return;
}

//Change matrix mode
void G3SIM_MtxMode(GXMtxMode mode)
{
    s_curMtxMode = mode;


    if( mode > GX_MTXMODE_TEXTURE )
    {
        printf( "Warning: Invalid matrix mode %d\n", s_curMtxMode );
    }
    return;
}

// Multiply the two independent DS position/vector accumulators independently.
// DS arithmetic wraps the 64-bit accumulator, then truncates the dot product.
// Logical shift gives identical low32 result bits for negative and positive sums.
static void NativeMatrixMult(G3SIM_Matrix_t *dst,const fx32 *src,int rows,int cols)
{
    G3SIM_Matrix_t old=*dst;
    for(int i=0;i<rows;i++)for(int j=0;j<4;j++){
        u64 sum=0;for(int k=0;k<cols;k++)sum+=(u64)((s64)src[i*cols+k]*old.nums[k][j]);
        if(cols==3 && i==3)sum+=(u64)((s64)FX32_ONE*old.nums[3][j]);
        dst->nums[i][j]=(fx32)(u32)(sum>>12);
    }
}
#ifdef OPT_MATH_MUL43
/* SS semantics (NativeMatrixMult): a u64 accumulator of exact 64-bit products, truncated once
   (>>12). A zero source entry adds exactly 0, so it is skipped; everything else is the same
   arithmetic. Rows are copied only as far as they are read. */
static void NativeMatrixMultFast(G3SIM_Matrix_t *dst,const fx32 *src,int rows,int cols)
{
    G3SIM_Matrix_t old;memcpy(old.nums,dst->nums,sizeof old.nums);
    for(int i=0;i<rows;i++){
        const fx32*a=src+i*cols;int ks[4],nk=0;for(int k=0;k<cols;k++)if(a[k])ks[nk++]=k;
        const bool t=(cols==3&&i==3);
        for(int j=0;j<4;j++){
            u64 sum=t?(u64)((s64)FX32_ONE*old.nums[3][j]):0;
            for(int n=0;n<nk;n++){int k=ks[n];sum+=(u64)((s64)a[k]*old.nums[k][j]);}
            dst->nums[i][j]=(fx32)(u32)(sum>>12);
        }
    }
}
#endif
static void NativeMatrixMultCurrent(const fx32 *src,int rows,int cols)
{
#ifdef OPT_MATH_MUL43
#ifdef OPT_MATH_ORACLE
    OMState om;OMSave(om);{G3SIM_Matrix_t*cm=OMCur(om);NativeMatrixMult(cm,src,rows,cols);if(s_curMtxMode==GX_MTXMODE_POSITION_VECTOR)NativeMatrixMult(&om.pos,src,rows,cols);}
#endif
    NativeMatrixMultFast(getCurrentMatrix(),src,rows,cols);
    if(s_curMtxMode==GX_MTXMODE_POSITION_VECTOR)NativeMatrixMultFast(&currentPositionMatrix,src,rows,cols);
#ifdef OPT_MATH_ORACLE
    OM_CHECK(2,OMCmp(om));
#endif
#else
    NativeMatrixMult(getCurrentMatrix(),src,rows,cols);
    if(s_curMtxMode==GX_MTXMODE_POSITION_VECTOR)NativeMatrixMult(&currentPositionMatrix,src,rows,cols);
#endif
    if(s_curMtxMode!=GX_MTXMODE_TEXTURE)calculateClipMatrix();
}
void G3SIM_MtxMult33(MtxFx33 *mtx){NativeMatrixMultCurrent((const fx32*)mtx,3,3);}
void G3SIM_MtxMult43(MtxFx43 *mtx){NativeMatrixMultCurrent((const fx32*)mtx,4,3);}
void G3SIM_MtxMult44(MtxFx44 *mtx){NativeMatrixMultCurrent((const fx32*)mtx,4,4);}

//TODO: Keep G3X_GXSTAT up to date
void G3SIM_MtxPop(u8 num)
{
    G3SIM_MatrixStack_t * curMtxStack = getCurrentMatrixStack();
    G3SIM_Matrix_t * currentMatrix = getCurrentMatrix();
    if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR
     || s_curMtxMode == GX_MTXMODE_POSITION )
    {
        //Perform stack operations on both position and vector matrices
        G3SIM_MatrixStack_t * positionMtxStack = getPositionMatrixStack();
        G3SIM_MatrixStack_t * vectorMtxStack = getVectorMatrixStack();
        if( num <= positionMtxStack->stackPtr)
        {
            positionMtxStack->stackPtr -= num;
        }
        else
        {
            positionMtxStack->stackPtr = 0;
        }

        if( num <= vectorMtxStack->stackPtr)
        {
            vectorMtxStack->stackPtr -= num;
        }
        else
        {
            vectorMtxStack->stackPtr = 0;
        }
        
        //Copy to current position and vector matrix
        memcpy( &currentPositionMatrix, &positionMtxStack->mtxs[positionMtxStack->stackPtr], sizeof(G3SIM_Matrix_t));
        memcpy( &currentVectorMatrix, &vectorMtxStack->mtxs[vectorMtxStack->stackPtr], sizeof(G3SIM_Matrix_t));
        calculateClipMatrix();
    }
    else
    {
        if( num <= curMtxStack->stackPtr )
        {
            curMtxStack->stackPtr -= num;
        }  
        else
        {
            curMtxStack->stackPtr = 0;
        }
        

        //Copy to current matrix
        memcpy( currentMatrix, &curMtxStack->mtxs[curMtxStack->stackPtr], sizeof(G3SIM_Matrix_t));
        if( s_curMtxMode == GX_MTXMODE_PROJECTION )
        {
            calculateClipMatrix();
        }
    }
    
    return;
}

void G3SIM_MtxPush()
{
    if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR
     || s_curMtxMode == GX_MTXMODE_POSITION )
    {
        //Position and vector
        G3SIM_MatrixStack_t * positionMtxStack = getPositionMatrixStack();
        G3SIM_MatrixStack_t * vectorMtxStack = getVectorMatrixStack();
        memcpy( &positionMtxStack->mtxs[positionMtxStack->stackPtr], &currentPositionMatrix, sizeof(G3SIM_Matrix_t) );
        memcpy( &vectorMtxStack->mtxs[vectorMtxStack->stackPtr], &currentVectorMatrix, sizeof(G3SIM_Matrix_t) );
        positionMtxStack->stackPtr++;
        vectorMtxStack->stackPtr++;
    }
    else
    {
        G3SIM_MatrixStack_t * curMtxStack = getCurrentMatrixStack();
        G3SIM_Matrix_t * currentMatrix = getCurrentMatrix();
        memcpy( &curMtxStack->mtxs[curMtxStack->stackPtr], currentMatrix, sizeof(G3SIM_Matrix_t) );
        curMtxStack->stackPtr++;
    }
    return;
}

void G3SIM_MtxRestore(u8 idx)
{
    G3SIM_MatrixStack_t * curMtxStack = getCurrentMatrixStack();
    G3SIM_Matrix_t * currentMatrix = getCurrentMatrix();
    if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR
     || s_curMtxMode == GX_MTXMODE_POSITION )
    {
        //Restore to both current position matrix and current vector matrix
        G3SIM_MatrixStack_t * positionMtxStack = getPositionMatrixStack();
        G3SIM_MatrixStack_t * vectorMtxStack = getVectorMatrixStack();
        memcpy( &currentPositionMatrix, &positionMtxStack->mtxs[idx], sizeof(G3SIM_Matrix_t) );
        memcpy( &currentVectorMatrix, &vectorMtxStack->mtxs[idx], sizeof(G3SIM_Matrix_t) );
        calculateClipMatrix();
    }
    else
    {
        memcpy( currentMatrix, &curMtxStack->mtxs[idx], sizeof(G3SIM_Matrix_t) );
        if( s_curMtxMode == GX_MTXMODE_PROJECTION )
        {
            calculateClipMatrix();
        }
    }


    return;
}

void G3SIM_MtxScale(fx32 * scale)
{
    // DS SCALE never modifies the vector matrix, even in POSITION_VECTOR mode.
    G3SIM_Matrix_t *matrix = s_curMtxMode == GX_MTXMODE_POSITION_VECTOR
        ? &currentPositionMatrix : getCurrentMatrix();
#ifdef OPT_MATH_SCALE
    {
#ifdef OPT_MATH_ORACLE
        OMState om;OMSave(om);{G3SIM_Matrix_t*cm=s_curMtxMode==GX_MTXMODE_POSITION_VECTOR?&om.pos:OMCur(om);for(int i=0;i<3;i++)for(int j=0;j<4;j++)cm->nums[i][j]=(fx32)(((s64)scale[i]*cm->nums[i][j])>>12);}
#endif
        /* ((s64)4096*m)>>12 == m exactly: a unit row is left unchanged. */
        for(int i=0;i<3;i++){fx32 f=scale[i];if(f==FX32_ONE)continue;fx32*r=matrix->nums[i];
            for(int j=0;j<4;j++)r[j]=(fx32)(((s64)f*r[j])>>12);}
#ifdef OPT_MATH_ORACLE
        OM_CHECK(1,OMCmp(om));
#endif
#ifdef OPT_MATH_SCALE_NOP
        /* A (1,1,1) scale leaves every matrix bit-identical: no new clip matrix/generation. */
        if(scale[0]==FX32_ONE&&scale[1]==FX32_ONE&&scale[2]==FX32_ONE)return;
#endif
        if(s_curMtxMode != GX_MTXMODE_TEXTURE)calculateClipMatrix();
        return;
    }
#endif
    for(int i=0;i<3;i++)for(int j=0;j<4;j++)
        matrix->nums[i][j] = (fx32)(((s64)scale[i]*matrix->nums[i][j])>>12);
    if(s_curMtxMode != GX_MTXMODE_TEXTURE)calculateClipMatrix();
}

void G3SIM_MtxStore(u8 idx)
{
    if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR
     || s_curMtxMode == GX_MTXMODE_POSITION )
    {
        G3SIM_MatrixStack_t * positionMtxStack = getPositionMatrixStack();
        G3SIM_MatrixStack_t * vectorMtxStack = getVectorMatrixStack();
        memcpy( &positionMtxStack->mtxs[idx], &currentPositionMatrix, sizeof(G3SIM_Matrix_t) );
        memcpy( &vectorMtxStack->mtxs[idx], &currentVectorMatrix, sizeof(G3SIM_Matrix_t) );
    }
    else
    {
        G3SIM_MatrixStack_t * curMtxStack = getCurrentMatrixStack();
        G3SIM_Matrix_t * currentMatrix = getCurrentMatrix();
        memcpy( &curMtxStack->mtxs[idx], currentMatrix, sizeof(G3SIM_Matrix_t) );
    }


    return;
}



void G3SIM_MtxTranslate(fx32* trans)
{
    G3SIM_Matrix_t *matrix=s_curMtxMode==GX_MTXMODE_POSITION_VECTOR?&currentPositionMatrix:getCurrentMatrix();
    for(int j=0;j<4;j++){
        u64 sum=0;for(int i=0;i<3;i++)sum+=(u64)((s64)trans[i]*matrix->nums[i][j]);
        matrix->nums[3][j]=(fx32)((u32)matrix->nums[3][j]+(u32)(sum>>12));
    }
    if(s_curMtxMode!=GX_MTXMODE_TEXTURE)calculateClipMatrix();
}

/* Lighting cache. The per-light terms below are the same float expressions the original
   per-normal code evaluated (same operand order), computed once when their inputs change:
   vector-matrix entries as floats, light vectors transformed by that matrix, and the
   material*light colour products. s_lightGen is bumped by LightColor/LightVector/DiffAmb/
   SpecEmi; the vector matrix is compared by value. */
static u32 s_lightCacheGen = 0;
static fx32 s_cacheVecMtx[3][3];
static float s_fVecMtx[3][3];
static float s_lightVecT[4][3];
static float s_specTerm[4][3], s_diffTerm[4][3], s_ambTerm[4][3], s_emisF[3];
static float s_normTab[1024]; static u8 s_normTabInit;
// A NORMAL command only publishes RGB in this frontend. Keep all derived
// lighting cache updates on hits; COLOR may have overwritten the published RGB.
#ifndef VITAPOKE_NORMAL_RESULT_CACHE
#define VITAPOKE_NORMAL_RESULT_CACHE 1
#endif
struct NormalResult {u32 data,epoch;u8 flags,color[3];};
static NormalResult normalResults[256];static u32 normalResultEpoch=1;
static void RefreshLightCache()
{
    int mtxChanged = 0;
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) if(s_cacheVecMtx[i][j] != currentVectorMatrix.nums[i][j]) mtxChanged = 1;
    if(mtxChanged) {
        for(int i=0;i<3;i++) for(int j=0;j<3;j++) { s_cacheVecMtx[i][j] = currentVectorMatrix.nums[i][j]; s_fVecMtx[i][j] = FX_FX32_TO_F32(currentVectorMatrix.nums[i][j]); }
    }
    if(mtxChanged || s_lightCacheGen != s_lightGen) {
#if VITAPOKE_NORMAL_RESULT_CACHE
        if(++normalResultEpoch==0){memset(normalResults,0,sizeof(normalResults));normalResultEpoch=1;}
#endif
        for(int l=0;l<4;l++) {
            s_lightVecT[l][0] = s_G3LightVector[l][0];
            s_lightVecT[l][1] = s_G3LightVector[l][1];
            s_lightVecT[l][2] = s_G3LightVector[l][2];
        }
    }
    if(s_lightCacheGen != s_lightGen) {
        s_lightCacheGen = s_lightGen;
        for(int c=0;c<3;c++) s_emisF[c] = (float)s_g3EmissionColor[c] / 255.0f;
        for(int l=0;l<4;l++) for(int c=0;c<3;c++) {
            s_specTerm[l][c] = ((float)s_g3SpecularColor[c] / 255.0f) * ((float)s_G3LightColor[l][c] / 255.0f);
            s_diffTerm[l][c] = ((float)s_g3DiffuseColor[c] / 255.0f) * ((float)s_G3LightColor[l][c] / 255.0f);
            s_ambTerm[l][c]  = ((float)s_g3AmbientColor[c] / 255.0f) * ((float)s_G3LightColor[l][c] / 255.0f);
        }
    }
}
void G3SIM_Normal(u32 data)
{
    if(!s_normTabInit) { for(int i=0;i<1024;i++) s_normTab[i] = DecodeSignedFx10(i); s_normTabInit = 1; }
    RefreshLightCache();
#if VITAPOKE_NORMAL_RESULT_CACHE
    NormalResult&result=normalResults[(data*2654435761u)>>24];
    if(result.epoch==normalResultEpoch&&result.data==data&&result.flags==s_curPolygonAttr.lightFlag){memcpy(s_g3CurColor,result.color,3);return;}
#endif
    // DS vectors are signed two's-complement 1.9 fixed-point components.
    float normalVectorIn[3];
    normalVectorIn[0] = s_normTab[data & 0x3FF];
    normalVectorIn[1] = s_normTab[(data >> 10) & 0x3FF];
    normalVectorIn[2] = s_normTab[(data >> 20) & 0x3FF];

    float normalVector[3];
    normalVector[0] = (normalVectorIn[0] * s_fVecMtx[0][0]) + (normalVectorIn[1] * s_fVecMtx[1][0]) + (normalVectorIn[2] * s_fVecMtx[2][0]);
    normalVector[1] = (normalVectorIn[0] * s_fVecMtx[0][1]) + (normalVectorIn[1] * s_fVecMtx[1][1]) + (normalVectorIn[2] * s_fVecMtx[2][1]);
    normalVector[2] = (normalVectorIn[0] * s_fVecMtx[0][2]) + (normalVectorIn[1] * s_fVecMtx[1][2]) + (normalVectorIn[2] * s_fVecMtx[2][2]);

    float color[3];
    color[0] = s_emisF[0]; color[1] = s_emisF[1]; color[2] = s_emisF[2];
    for(int lightNum=0; lightNum<4; lightNum++) {
        if((s_curPolygonAttr.lightFlag >> lightNum) & 1) {
            const float * lv = s_lightVecT[lightNum];
            float diffuseLevel = (lv[0] * normalVector[0]) + (lv[1] * normalVector[1]) + (lv[2] * normalVector[2]);
            diffuseLevel = diffuseLevel * -1.0f;
            if(diffuseLevel < 0.0f) diffuseLevel = 0.0f;
            color[0] = color[0] + s_specTerm[lightNum][0];
            color[1] = color[1] + s_specTerm[lightNum][1];
            color[2] = color[2] + s_specTerm[lightNum][2];
            color[0] = color[0] + (s_diffTerm[lightNum][0] * diffuseLevel);
            color[1] = color[1] + (s_diffTerm[lightNum][1] * diffuseLevel);
            color[2] = color[2] + (s_diffTerm[lightNum][2] * diffuseLevel);
            color[0] = color[0] + s_ambTerm[lightNum][0];
            color[1] = color[1] + s_ambTerm[lightNum][1];
            color[2] = color[2] + s_ambTerm[lightNum][2];
        }
    }
    if(color[0] > 1.0f) color[0] = 1.0f;
    if(color[1] > 1.0f) color[1] = 1.0f;
    if(color[2] > 1.0f) color[2] = 1.0f;
    s_g3CurColor[0] = (u8)(color[0] * 255.0f);
    s_g3CurColor[1] = (u8)(color[1] * 255.0f);
    s_g3CurColor[2] = (u8)(color[2] * 255.0f);
#if VITAPOKE_NORMAL_RESULT_CACHE
    result.data=data;result.epoch=normalResultEpoch;result.flags=s_curPolygonAttr.lightFlag;memcpy(result.color,s_g3CurColor,3);
#endif
}

//PolygonAttr
void G3SIM_PolygonAttr(u32 data)
{
    u8 lightFlag = data & 0b1111;
    u8 alpha = (data & 0b111110000000000000000) >> 16;
    u8 polygonMode = (data & 0b110000) >> 4;
    u8 cullMode = (data & 0b11000000) >> 6;
    u8 depthTest = (data & 0b100000000000000) >> 14;
    u8 useFog = (data & 0b1000000000000000) >> 15;
    u8 transDepth = (data >> 11) & 1;

    if(cullMode != s_curPolygonAttr.cullMode || polygonMode != s_curPolygonAttr.polygonMode || alpha != s_curPolygonAttr.alphaInt || depthTest != s_curPolygonAttr.depthTest || useFog != s_curPolygonAttr.fogEnable || transDepth != s_SIM_transDepthWrite)
    {
        // Flush the vertex buffer
        G3SIM_FlushArray();
    }

    if(depthTest != s_curPolygonAttr.depthTest)
    {
        if(depthTest){
            glDepthFunc(GL_LEQUAL);
        } else {
            glDepthFunc(GL_LESS);
        }
    }

    s_curPolygonAttr.lightFlag = lightFlag;
    s_curPolygonAttr.cullMode = cullMode;
    s_curPolygonAttr.polygonMode = polygonMode;
    s_curPolygonAttr.alphaInt = alpha;
    s_curPolygonAttr.depthTest = depthTest;
    s_curPolygonAttr.fogEnable = useFog;
    s_SIM_transDepthWrite = transDepth;

    if(alpha == 31){
        s_curPolygonAttr.alpha = 1.0f;
    } else {
        s_curPolygonAttr.alpha = (float)alpha / 31.0f;
    }

    switch(cullMode) {
        case GX_CULL_ALL:
            glEnable( GL_CULL_FACE );
            glCullFace(GL_FRONT_AND_BACK);
            break;
        case GX_CULL_FRONT:
            glEnable( GL_CULL_FACE );
            glCullFace(GL_BACK);
            break;
        case GX_CULL_BACK:
            glEnable( GL_CULL_FACE );
            glCullFace(GL_FRONT);
            break;
        case GX_CULL_NONE:
            glDisable( GL_CULL_FACE );
            break;
    }


    return;
}

void G3SIM_SpecEmi(u32 data)
{
    s_lightGen++;
    u8 specularRed = (data & 0b11111);
    u8 specularGreen = (data & 0b1111100000) >> 5;
    u8 specularBlue = (data & 0b111110000000000) >> 10;
    u8 emissionRed = (data & 0b111110000000000000000) >> 16;
    u8 emissionGreen = (data & 0b11111000000000000000000000) >> 21;
    u8 emissionBlue = (data & 0b1111100000000000000000000000000) >> 26;

    // Bit 15 selects the shininess table; it does not set current vertex RGB.
    s_g3SpecularColor[0] = specularRed << 3;
    s_g3SpecularColor[1] = specularGreen << 3;
    s_g3SpecularColor[2] = specularBlue << 3;

    s_g3EmissionColor[0] = emissionRed << 3;
    s_g3EmissionColor[1] = emissionGreen << 3;
    s_g3EmissionColor[2] = emissionBlue << 3;
}

//SwapBuffers
void G3SIM_SwapBuffers(u8 depthBufferMode)
{
    s_SIM_useWBuffer = depthBufferMode;
#ifdef OPT_MATH_ORACLE
    {static unsigned of;if(++of%300==0)printf("[OPT-ORACLE] frame=%u clip=%u/%u scale=%u/%u mul43=%u/%u box=%u/%u boxfallback=%u corners=%u/%u lazyfin=%u/%u\n",of,omMis[0],omChecks[0],omMis[1],omChecks[1],omMis[2],omChecks[2],omMis[3],omChecks[3],omChecks[4],omMis[5],omChecks[5],omMis[6],omChecks[6]);}
#endif
#ifdef OPT_MATH_PROF
    static unsigned f;if(++f%30==0){printf("[OMP] pendfinish=%u(%u) clipcompute=%u scale_unit=%u scale_posvec=%u scale=%u box_inside=%u box_zrej=%u box_other=%u\n",ompUs[0]/30,omp[0]/30,omp[1]/30,omp[2]/30,omp[3]/30,omp[4]/30,omp[5]/30,omp[6]/30,omp[7]/30);memset(omp,0,sizeof omp);memset(ompUs,0,sizeof ompUs);}
#endif
    s_swapBuffersCalledThisFrame = TRUE;
}

u8 G3SIM_SwapBuffersCalledThisFrame()
{
    return s_swapBuffersCalledThisFrame;
}

void G3SIM_ResetSwapBuffersCall()
{
    s_swapBuffersCalledThisFrame = FALSE;
}

//TexCoord
void G3SIM_TexCoord(u32 data)
{
    s16 s = data & 0xFFFF;
    s16 t = (data & 0xFFFF0000) >> 16;

    //Multiply by the texture matrix
    //See GBATek section on Texture Matrix
    if(s_texImageParam.texCordTransform == 1) {
        float texCoordVector[4];
        texCoordVector[0] = (float)s / 16.0f;
        texCoordVector[1] = (float)t / 16.0f;
        texCoordVector[2] = 1.0f / 16.0f;
        texCoordVector[3] = 1.0f / 16.0f;

        G3SIM_Matrix_t * textureMtx = &currentTextureMatrix;


        s_g3NextTexCoordS = (texCoordVector[0] * FX_FX32_TO_F32(textureMtx->nums[0][0]))
                          + (texCoordVector[1] * FX_FX32_TO_F32(textureMtx->nums[1][0]))
                          + (texCoordVector[2] * FX_FX32_TO_F32(textureMtx->nums[2][0]))
                          + (texCoordVector[3] * FX_FX32_TO_F32(textureMtx->nums[3][0]));

        s_g3NextTexCoordT = (texCoordVector[0] * FX_FX32_TO_F32(textureMtx->nums[0][1]))
                          + (texCoordVector[1] * FX_FX32_TO_F32(textureMtx->nums[1][1]))
                          + (texCoordVector[2] * FX_FX32_TO_F32(textureMtx->nums[2][1]))
                          + (texCoordVector[3] * FX_FX32_TO_F32(textureMtx->nums[3][1]));
    } else {
        s_g3NextTexCoordS = ((float)s)/16.0f;
        s_g3NextTexCoordT = ((float)t)/16.0f;
    }

    return;
}

//TexImageParam
void G3SIM_TexImageParam(u32 data)
{
    s_texImageParam.textureOffset = ((u16)data) << 3;
    s_texImageParam.repeatS = (data & 0b10000000000000000) >> 16;
    s_texImageParam.repeatT = (data & 0b100000000000000000) >> 17;
    s_texImageParam.flipS = (data & 0b1000000000000000000) >> 18;
    s_texImageParam.flipT = (data & 0b10000000000000000000) >> 19;
    s_texImageParam.textureSSize = 8 << ((data & 0b11100000000000000000000) >> 20);
    s_texImageParam.textureTSize = 8 << ((data & 0b11100000000000000000000000) >> 23);
    s_texImageParam.textureFormat = (data & 0b11100000000000000000000000000) >> 26;
    s_texImageParam.color0 = (data & 0b100000000000000000000000000000) >> 29;
    s_texImageParam.texCordTransform = (data & 0b11000000000000000000000000000000) >> 30;
    return;
}

//TexPlttBase
void G3SIM_TexPlttBase(u32 data)
{
    if( s_texImageParam.textureFormat == 2 )
    {
        s_texPlttBase = data << 3;
    }
    else
    {
        s_texPlttBase = data << 4;
    }
    return;
}

/* Per-slot derived data: the screen-space/colour values the original SubmitPolygon +
   G3SIM_AddVtx computed per polygon vertex, now computed once per vertex with the same
   expressions (from the 24-bit truncated w the original truncated in place before use).
   Z-buffer z depends only on the vertex; W-buffer z depends on the polygon's wsize and is
   still computed per polygon over all four physical slots, exactly as before. */
#include "g3_listcache.h"
static G3SIM_SlotDerived s_der[4];
static float s_rgbTab[256];
static unsigned s_chanTab[256]; static u8 s_chanTabInit;
static inline unsigned G3SIM_Channel(float f) { if(f<0)f=0;if(f>1)f=1;return unsigned(f*255.0f+0.5f); }
/* Preserve all public slot fields while carrying the derived cache with copies. */
#ifdef OPT_MATH_LAZYFIN
/* A vertex still pending when the matrix changes is finished later from a copy of the clip
   matrix it was pending under (the eager code finished it immediately with that same matrix).
   Slots overwritten or restored before anyone reads them are never finished. */
static G3SIM_Matrix_t s_finSnap[4];static signed char s_slotSnap[4]={-1,-1,-1,-1};
#ifdef OPT_MATH_ORACLE
static G3SIM_FxVtx_t s_shV[4];static G3SIM_SlotDerived s_shD[4];
#endif
#endif
static inline void CopySlot(int d, int sidx) {
    s_g3PolygonVerts[d]=s_g3PolygonVerts[sidx];
    s_der[d] = s_der[sidx];
#ifdef OPT_MATH_LAZYFIN
    s_slotSnap[d]=s_slotSnap[sidx];
#ifdef OPT_MATH_ORACLE
    s_shV[d]=s_shV[sidx];s_shD[d]=s_shD[sidx];
#endif
#endif
}
extern "C" unsigned s_numG3DrawsThisFrame;

static float VertexZDepth(s32 fxZ,s32 wt){
        s32 zz;
        if(wt) {
            s32 quotient;
            if(fxZ >= -131072 && fxZ <= 131071)
                quotient = (fxZ * 16384) / wt;
            else
                quotient = ((s64)fxZ * 0x4000) / wt;
            zz = (s32)(((u32)quotient + 0x3FFFu) * 0x200u);
        } else {
            zz = 0x7FFE00;
        }
        if(zz < 0) zz = 0; else if(zz > 0xFFFFFF) zz = 0xFFFFFF;
        return (((float)zz / 16777215.0f) * 2.0f) - 1.0f;
}

//Add a vertex
static void NativeRawVtx(s16 x, s16 y, s16 z)
{
    if(!s_chanTabInit) { for(int i=0;i<256;i++){s_rgbTab[i]=(float)i/255.0f;s_chanTab[i]=G3SIM_Channel(s_rgbTab[i]);} s_chanTabInit = 1; }
    fx32 fxX = x, fxY = y, fxZ = z, fxW = FX32_ONE;
    // Render-only raw geometry; integer Box/Pos paths retain calculateVtxFx.


    G3SIM_FxVtx_t * v = &s_g3PolygonVerts[s_G3numInPoly];
    G3SIM_SlotDerived * d = &s_der[s_G3numInPoly];
    v->x = fxX; v->y = fxY; v->z = fxZ; v->w = fxW;
    v->r=s_rgbTab[s_g3CurColor[0]];v->g=s_rgbTab[s_g3CurColor[1]];v->b=s_rgbTab[s_g3CurColor[2]];v->a=s_curPolygonAttr.alpha;
    v->s = s_g3NextTexCoordS / (float)s_texImageParam.textureSSize;
    v->t = s_g3NextTexCoordT / (float)s_texImageParam.textureTSize;

    d->sx=x/4096.0f;d->sy=y/4096.0f;d->sz=z/4096.0f;d->gen=nativeMatrixGen;d->finished=0;d->zValid=0;
#ifdef OPT_MATH_LAZYFIN
    s_slotSnap[s_G3numInPoly]=-1;
#endif
    d->color = s_chanTab[s_g3CurColor[0]] | (s_chanTab[s_g3CurColor[1]] << 8) | (s_chanTab[s_g3CurColor[2]] << 16) | (G3SIM_Channel(s_curPolygonAttr.alpha) << 24);

    s_G3numInPoly++;
    if( s_primType == GX_BEGIN_QUADS )
    {
        if( s_G3numInPoly >= 4 )
        {
            s_G3numInPoly = 0;
        }       
    }
    if( s_primType == GX_BEGIN_TRIANGLES )
    {
        if( s_G3numInPoly >= 3 )
        {
            s_G3numInPoly = 0;
        }
    }
    if( s_primType == GX_BEGIN_QUAD_STRIP )
    {        
        if( s_G3numInPoly >= 4 )
        {
            s_G3numInPoly = 2;
            s_G3drawPoly = 1;
            s_G3quadStripTriNum++;
        }
    }
    if( s_primType == GX_BEGIN_TRIANGLE_STRIP )
    {
        if( s_G3numInPoly >= 3 )
        {
            s_G3numInPoly = 2;
            s_G3drawPoly = 1;
            s_G3triStripTriNum++;
        }
    }
    return;
}

static void NativeFinishSlot(int i){
 auto&d=s_der[i];if(d.finished||!d.gen)return;auto&v=s_g3PolygonVerts[i];
#ifdef OPT_MATH_LAZYFIN
 if(s_slotSnap[i]>=0){calculateVtxFxM(s_finSnap[s_slotSnap[i]],&v.x,&v.y,&v.z,&v.w);s_slotSnap[i]=-1;}else
#endif
 calculateVtxFx(&v.x,&v.y,&v.z,&v.w);d.finished=1;
 float x=FX_FX32_TO_F32(v.x),y=FX_FX32_TO_F32(v.y),w=FX_FX32_TO_F32(v.w&0xffffff);
 float sx=((x+w)*256.0f)/(2*w),sy=((y+w)*192.0f)/(2*w);d.sx=sx/128.0f-1.0f;d.sy=sy/96.0f-1.0f;
 d.oob=(d.sx>50.0f||d.sx<-50.0f||d.sy>50.0f||d.sy<-50.0f);d.zValid=0;
}
static void NativeFinishPendingVertices(){for(int i=0;i<4;i++)if(s_der[i].gen==nativeMatrixGen)NativeFinishSlot(i);}
#ifdef OPT_MATH_LAZYFIN
#ifdef OPT_MATH_ORACLE
static void NativeLazyCheck(int i){if(s_slotSnap[i]<0)return;NativeFinishSlot(i);OM_CHECK(6,memcmp(&s_shV[i],&s_g3PolygonVerts[i],sizeof s_shV[i])|memcmp(&s_shD[i],&s_der[i],sizeof s_shD[i]));}
#endif
static void NativeDeferPendingVertices(){
 int used=0;for(int i=0;i<4;i++)if(s_slotSnap[i]>=0)used|=1<<s_slotSnap[i];
 int k=0;while(used>>k&1)k++;   /* at most three snapshots are referenced by non-pending slots */
 memcpy(&s_finSnap[k],&currentClipMatrix,sizeof currentClipMatrix);
 for(int i=0;i<4;i++)if(s_der[i].gen==nativeMatrixGen&&!s_der[i].finished){
#ifdef OPT_MATH_ORACLE
  {G3SIM_FxVtx_t v0=s_g3PolygonVerts[i];G3SIM_SlotDerived d0=s_der[i];NativeFinishSlot(i);s_shV[i]=s_g3PolygonVerts[i];s_shD[i]=s_der[i];s_g3PolygonVerts[i]=v0;s_der[i]=d0;}
#endif
  s_slotSnap[i]=(signed char)k;}
}
#endif
static bool NativeHasPendingVertices(){for(int i=0;i<4;i++)if(s_der[i].gen==nativeMatrixGen&&!s_der[i].finished)return true;return false;}
// Keep generic public callers on exact transformed-slot semantics.
void G3SIM_Vtx(s16 x,s16 y,s16 z){int i=s_G3numInPoly;NativeRawVtx(x,y,z);NativeFinishSlot(i);}

static unsigned hwPoly,hwMixed,hwOld;unsigned g3NonRawSubmits;
extern "C" void VitaNativeHWStats(unsigned f){printf("[HW-ELIGIBLE] frame=%u polys=%u mixed=%u old=%u\n",f,hwPoly,hwMixed,hwOld);hwPoly=hwMixed=hwOld=0;}
static void G3SIM_SubmitPolygonOrdered(G3SIM_FxVtx_t * fxVerts, int numVerts, const u8 * order);
extern "C" void VitaNativeRawMode(int);
void G3SIM_SubmitPolygon(G3SIM_FxVtx_t * fxVerts, int numVerts) {
 VitaNativeRawMode(0);

    G3SIM_Vertex_t glVerts[6];
    u8 addPolygon = TRUE;
    int numFinalVerts = numVerts;

    if(numVerts < 3 || numVerts > 4) {
        SIM_assert_always();
        return;
    }

    for(int i=0; i < numVerts; i++) {
        G3SIM_FxVtx_t * curVtx = &fxVerts[i];
    
        // Truncate W to 24 bits
        curVtx->w &= 0x00FFFFFF;
    
        float floatX, floatY, floatW;
        float screenX, screenY;
    
        floatX = FX_FX32_TO_F32(curVtx->x);
        floatY = FX_FX32_TO_F32(curVtx->y);
        floatW = FX_FX32_TO_F32(curVtx->w);
    
        screenX = (((floatX+floatW)*256.0f) / (2*floatW));
        screenY = (((floatY+floatW)*192.0f) / (2*floatW));
        					
        screenX = (screenX / 128.0f) - 1.0f;
        screenY = (screenY / 96.0f) - 1.0f;
    
        glVerts[i].x = screenX;
        glVerts[i].y = screenY;
        glVerts[i].s = curVtx->s;
        glVerts[i].t = curVtx->t;
        glVerts[i].r = curVtx->r;
        glVerts[i].g = curVtx->g;
        glVerts[i].b = curVtx->b;
        glVerts[i].a = curVtx->a;
    }

    // Get the W size and remove the polygon if W is 0
    u32 wsize = 0;
    for(int i=0; i < 4; i++) {
        G3SIM_FxVtx_t * curVtx = &fxVerts[i];
        u32 w = curVtx->w;
        if(w == 0){
            addPolygon = FALSE;
        }
        while ((w >> wsize) && (wsize < 32)) {
            wsize += 4;
        }
    }

    // Normalize W
    for(int i=0; i < 4; i++) {
        G3SIM_FxVtx_t * curVtx = &fxVerts[i];
        
        s32 w, wshifted;
        
        if(wsize < 16) {
        	w = curVtx->w << (16-wsize);
        	wshifted = w >> (16-wsize);
        } else {
        	w = curVtx->w >> (wsize - 16);
        	wshifted = w << (wsize - 16);
        }
        
        s32 z;
        
        if(curVtx->w) {
        	// Common clip-space Z fits a signed 32-bit numerator. Use exact
            // native signed division there; retain 64-bit fallback otherwise.
            s32 quotient;
            if(curVtx->z >= -131072 && curVtx->z <= 131071)
                quotient = (curVtx->z * 16384) / curVtx->w;
            else
                quotient = ((s64)curVtx->z * 0x4000) / curVtx->w;
            z = (s32)(((u32)quotient + 0x3FFFu) * 0x200u);
        } else {
        	z = 0x7FFE00;
        }
        
        if(s_SIM_useWBuffer) {
        	z = wshifted;
        }
        
        if(z < 0) {
        	z = 0;
        } else if(z > 0xFFFFFF) {
        	z = 0xFFFFFF;
        }
        
        glVerts[i].z = (((float)z / 16777215.0f) * 2.0f) - 1.0f;
        glVerts[i].w = (((float)w / 16777215.0f) * 2.0f) - 1.0f;
    }

    // If we have a quad, convert it into 2 triangles
    if(numVerts == 4) {
        glVerts[5] = glVerts[0];
        glVerts[4] = glVerts[3];
        glVerts[3] = glVerts[2];
        numFinalVerts = 6;
    }

    // Check if any of the verts are horribly out of bounds
    // TODO: This is a HACK and we should eventually remove it
    for(int i=0; i < numFinalVerts; i++) {
        if(glVerts[i].x > 50.0f || glVerts[i].x < -50.0f) {
            addPolygon = FALSE;
        }
        if(glVerts[i].y > 50.0f || glVerts[i].y < -50.0f) {
            addPolygon = FALSE;
        }
    }

    if(addPolygon) {
        for(int i=0; i < numFinalVerts; i++) {
            G3SIM_AddVtx(&glVerts[i]);
        }
    }


}

/* order[] maps the polygon's vertex order onto physical slots. The original code achieved
   this by physically swapping slots before submitting (and swapping back); every per-slot
   computation here (w truncation, w==0/wsize over all four slots, z, bounds) is
   order-independent, only the emission order uses it. */
extern "C" void VitaNativeRawMatrix(const s32*,unsigned);
static void NativeSubmitExact(G3SIM_FxVtx_t * fxVerts, int numVerts, const u8 * order) {
    u8 addPolygon = TRUE;
    if(numVerts < 3 || numVerts > 4) {
        SIM_assert_always();
        return;
    }
    // Truncate W to 24 bits (in place, as before)
    for(int i=0; i < numVerts; i++) fxVerts[i].w &= 0x00FFFFFF;

    // Get the W size and remove the polygon if W is 0 (all four physical slots, as before)
    u32 wsize = 0;
    for(int i=0; i < 4; i++) {
        u32 w = fxVerts[i].w;
        if(w == 0){
            addPolygon = FALSE;
        } else {
            /* Closed form of: while ((w >> wsize) && (wsize < 32)) wsize += 4;
               i.e. the smallest multiple of 4 >= the bit length of w (max 32), never below the
               value reached by earlier slots. */
            u32 need = ((32u - (u32)__builtin_clz(w)) + 3u) & ~3u;
            if(need > wsize) wsize = need;
        }
    }
    float zf[4];
    if(s_SIM_useWBuffer) {
        for(int i=0; i < 4; i++) {
            s32 w, wshifted;
            if(wsize < 16) {
                w = fxVerts[i].w << (16-wsize);
                wshifted = w >> (16-wsize);
            } else {
                w = fxVerts[i].w >> (wsize - 16);
                wshifted = w << (wsize - 16);
            }
            s32 z = wshifted;
            if(z < 0) {
                z = 0;
            } else if(z > 0xFFFFFF) {
                z = 0xFFFFFF;
            }
            zf[i] = (((float)z / 16777215.0f) * 2.0f) - 1.0f;
        }
    } else {
        for(int i=0; i < numVerts; i++) {
            if(!s_der[i].zValid){s_der[i].zf=VertexZDepth(fxVerts[i].z,fxVerts[i].w);s_der[i].zValid=1;}
            zf[i] = s_der[i].zf;
        }
    }
    for(int i=0; i < numVerts; i++) if(s_der[i].oob) addPolygon = FALSE;

    if(addPolygon) {
        static const u8 triEmit[3] = {0,1,2}, quadEmit[6] = {0,1,2,2,3,0};
        const u8 * emit = numVerts == 4 ? quadEmit : triEmit;
        int n = numVerts == 4 ? 6 : 3;
        float ss = (float)s_texImageParam.textureSSize, ts = (float)s_texImageParam.textureTSize;
        for(int k=0; k < n; k++) {
            int i = order[emit[k]];
            const G3SIM_SlotDerived & d = s_der[i];
            G3SIM_AppendVtx(fxVerts[i].s*ss, fxVerts[i].t*ts, d.color, 128.0f+d.sx*128.0f, 96.0f-d.sy*96.0f, zf[i]*32767.0f+32768.0f);
        }
    }
}

extern "C" void VitaNativeRawMode(int);
static void G3SIM_SubmitPolygonOrdered(G3SIM_FxVtx_t * fxVerts,int numVerts,const u8 *order){
 ++hwPoly;bool raw=VITAPOKE_GE_TRANSFORM;for(int i=0;i<numVerts;i++)if(s_der[i].gen!=nativeMatrixGen||s_der[i].finished)raw=false;
 if(!raw){++hwMixed;++g3NonRawSubmits;
#if defined(OPT_MATH_LAZYFIN)&&defined(OPT_MATH_ORACLE)
  for(int i=0;i<numVerts;i++)NativeLazyCheck(i);
#endif
  for(int i=0;i<numVerts;i++)NativeFinishSlot(i);VitaNativeRawMode(0);NativeSubmitExact(fxVerts,numVerts,order);return;}
 EnsureClipMatrix();VitaNativeRawMatrix(&currentClipMatrix.nums[0][0],nativeMatrixGen);
 static const u8 triEmit[3]={0,1,2},quadEmit[6]={0,1,2,2,3,0};const u8*emit=numVerts==4?quadEmit:triEmit;int n=numVerts==4?6:3;
 for(int k=0;k<n;k++){int i=order[emit[k]];const auto&d=s_der[i];G3SIM_AppendVtx(fxVerts[i].s,fxVerts[i].t,d.color,d.sx,d.sy,d.sz);}
}

/* Vertex + primitive assembly (moved here from the frontend's VTX16 case; same slot
   operations, expressed as slot copies/swaps that carry the derived data along). */
extern "C" void G3SIM_VtxAssemble(s16 x, s16 y, s16 z)
{
    NativeRawVtx(x, y, z);
    if (s_G3numInPoly == 0 || s_G3drawPoly == 1) {
        s_G3drawPoly = 0;
        if (s_primType == GX_BEGIN_TRIANGLES || s_primType == GX_BEGIN_TRIANGLE_STRIP) {
            /* Original: even strip triangles swapped slots 1<->2, submitted, swapped back, then
               shifted [0]=[1],[1]=[2]. Net physical state after: (s1,s2,s2,s3); emission order
               (0,2,1) for even, (0,1,2) for odd. */
            static const u8 odd[4] = {0,1,2,3}, even[4] = {0,2,1,3};
            int evenStrip = (s_primType == GX_BEGIN_TRIANGLE_STRIP && !(s_G3triStripTriNum & 1));
            G3SIM_SubmitPolygonOrdered(s_g3PolygonVerts, 3, evenStrip ? even : odd);
            if (s_primType == GX_BEGIN_TRIANGLE_STRIP) { CopySlot(0, 1); CopySlot(1, 2); }
            s_numG3DrawsThisFrame++;
        }
        if (s_primType == GX_BEGIN_QUADS || s_primType == GX_BEGIN_QUAD_STRIP) {
            /* Original quad strip: swap slots 2<->3 (a,b,d,c), submit, then [0]=[3],[1]=[2]
               giving (c,d,d,c). Same emission order and same final physical state here. */
            static const u8 quad[4] = {0,1,2,3}, quadStrip[4] = {0,1,3,2};
            int strip = (s_primType == GX_BEGIN_QUAD_STRIP);
            G3SIM_SubmitPolygonOrdered(s_g3PolygonVerts, 4, strip ? quadStrip : quad);
            s_numG3DrawsThisFrame++;
            if (strip) { CopySlot(0, 2); CopySlot(1, 3); CopySlot(2, 3); CopySlot(3, 0); }
        }
    }
}

/* ---- Display-list geometry cache hooks (see frontend.cpp ProcessCommandList) ----
   A "pure" list (only NOP, COLOR, NORMAL, TEXCOORD, VTX, BEGIN, END) produces GE vertices that
   depend only on its bytes and this entry state; the exit state is the primitive slot
   state it leaves behind. Both are snapshotted bytewise (memset first so padding compares). */
#include <cstring>
/* deps: which parts of the entry state the list's output actually depends on (decided while
   recording): a vertex before the first TEXCOORD/COLOR/NORMAL uses the entry texcoord/colour,
   NORMAL uses the lights, VTX_XY/XZ/YZ/DIFF before a full vertex uses prevXYZ (frontend). */
extern "C" void G3SIM_CaptureEntryState(G3GeomEntryState*s,unsigned deps){
 memset(s,0,sizeof*s);s->alpha=s_curPolygonAttr.alpha;s->sSize=s_texImageParam.textureSSize;s->tSize=s_texImageParam.textureTSize;
 if(deps&G3DEP_COLOR)memcpy(s->color,s_g3CurColor,3);
 if(deps&G3DEP_LIGHT){s->lightFlag=s_curPolygonAttr.lightFlag;
  memcpy(s->lightColor,s_G3LightColor,sizeof s->lightColor);memcpy(s->lightVector,s_G3LightVector,sizeof s->lightVector);memcpy(s->diffuse,s_g3DiffuseColor,3);memcpy(s->ambient,s_g3AmbientColor,3);memcpy(s->specular,s_g3SpecularColor,3);memcpy(s->emission,s_g3EmissionColor,3);
  for(int i=0;i<3;i++)for(int j=0;j<3;j++)s->vec[i][j]=currentVectorMatrix.nums[i][j];}
 if(deps&G3DEP_TEX){s->texS=s_g3NextTexCoordS;s->texT=s_g3NextTexCoordT;}
 if(deps&G3DEP_TEXCOORD){s->transform=s_texImageParam.texCordTransform;
  if(s->transform==1)for(int i=0;i<4;i++){s->texMtx[i][0]=currentTextureMatrix.nums[i][0];s->texMtx[i][1]=currentTextureMatrix.nums[i][1];}}
}
extern "C" int G3SIM_EntryStateMatches(const G3GeomEntryState*s,unsigned deps){G3GeomEntryState c;G3SIM_CaptureEntryState(&c,deps);return memcmp(&c,s,sizeof c)==0;}
extern "C" void G3SIM_CaptureSlots(G3SlotState*s){
#ifdef OPT_MATH_LAZYFIN
 for(int i=0;i<4;i++)if(s_slotSnap[i]>=0){
#ifdef OPT_MATH_ORACLE
  NativeLazyCheck(i);
#else
  NativeFinishSlot(i);
#endif
 }
#endif
 memset(s,0,sizeof*s);s->prim=s_primType;s->numInPoly=s_G3numInPoly;s->drawPoly=s_G3drawPoly;s->tri=s_G3triStripTriNum;s->quad=s_G3quadStripTriNum;s->gen=nativeMatrixGen;
 memcpy(s->verts,s_g3PolygonVerts,sizeof s->verts);memcpy(s->der,s_der,sizeof s->der);memcpy(s->color,s_g3CurColor,3);s->texS=s_g3NextTexCoordS;s->texT=s_g3NextTexCoordT;
}
extern "C" void G3SIM_RestoreSlots(const G3SlotState*s){
 s_primType=s->prim;s_G3numInPoly=s->numInPoly;s_G3drawPoly=s->drawPoly;s_G3triStripTriNum=s->tri;s_G3quadStripTriNum=s->quad;
 memcpy(s_g3PolygonVerts,s->verts,sizeof s_g3PolygonVerts);memcpy(s_der,s->der,sizeof s_der);memcpy(s_g3CurColor,s->color,3);s_g3NextTexCoordS=s->texS;s_g3NextTexCoordT=s->texT;
 /* Slot generations are relative to the matrix generation of the recording: current-at-record
    stays current, anything older stays "not current" (non-zero), empty stays empty. */
 for(int i=0;i<4;i++){unsigned g=s->der[i].gen;s_der[i].gen=g==0?0:(g==s->gen?nativeMatrixGen:nativeMatrixGen-1);}
#ifdef OPT_MATH_LAZYFIN
 for(int i=0;i<4;i++)s_slotSnap[i]=-1;
#endif
}
extern "C" int G3SIM_ReplayEligible(){return VITAPOKE_GE_TRANSFORM;}
extern "C" void G3SIM_ReplayRawBegin(){EnsureClipMatrix();VitaNativeRawMatrix(&currentClipMatrix.nums[0][0],nativeMatrixGen);}
