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
/* Set whenever anything G3SIM_Normal's cached terms are built from changes; see the
 * lighting cache further down. */
static int s_lightCacheDirty = 1;

/* How much geometry the game actually sends, so that the time the profiler attributes to the map
 * renderer can be divided by something. Plain counters: two increments on a path that is already
 * hundreds of instructions. */
extern "C" { unsigned g3VtxCalls, g3NormalCalls; }
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
/* The same colour the vertex path actually wants. Every vertex converted the three bytes above back
 * to float and scaled them, which is work that belongs where the colour is set -- once per lighting
 * command or per colour command -- rather than once per vertex. Kept beside the bytes, which the
 * rest of the simulator still reads. */
float s_g3CurColorF[3] = {0.0f, 0.0f, 0.0f};

static inline void G3SIM_SetCurColor(unsigned r, unsigned g, unsigned b)
{
    s_g3CurColor[0] = (u8)r;
    s_g3CurColor[1] = (u8)g;
    s_g3CurColor[2] = (u8)b;
    s_g3CurColorF[0] = (float)r * (1.0f / 255.0f);
    s_g3CurColorF[1] = (float)g * (1.0f / 255.0f);
    s_g3CurColorF[2] = (float)b * (1.0f / 255.0f);
}
u8 s_g3DiffuseColor[3] = {0};
u8 s_g3AmbientColor[3] = {0};
u8 s_g3SpecularColor[3] = {0};
u8 s_g3EmissionColor[3] = {0};
G3SIM_PolygonAttr_t s_curPolygonAttr;
/* One over the current texture's width and height; see G3SIM_TexImageParam. */
static float s_texInvSSize = 1.0f / 8.0f, s_texInvTSize = 1.0f / 8.0f;

static u8 s_swapBuffersCalledThisFrame;

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
                //ClipSegment((fx32*)&temp[c], &vtx[0], vprev, comp, 1, attribs);
                c++;
            }

            fx32 * vnext = verts[next];
            if(vnext[comp] <= vnext[3]) {
                //ClipSegment((fx32*)&temp[c], &vtx[0], vnext, comp, 1, attribs);
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
                //ClipSegment((fx32*)&verts[c], &vtx[0], vprev, comp, -1, attribs);
                c++;
            }

            fx32 * vnext = temp[next];
            if(vnext[comp] >= -vnext[3]) {
                //ClipSegment((fx32*)&verts[c], &vtx[0], vnext, comp, -1, attribs);
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
    /* Every matrix command asks for the current matrix before changing it, so this one line covers
     * translate, scale, multiply, load and identity without having to find each of them. Callers
     * that only read cost a rebuild that changes nothing. */
    s_lightCacheDirty = 1;
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
static void calculateClipMatrix()
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
                curSum = curSum + FX_Mul(currentPositionMatrix.nums[i][k], currentProjectionMatrix.nums[k][j]);
            }
            currentClipMatrix.nums[i][j] = curSum;
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

static void calculateVtxFx(fx32 * x, fx32 * y, fx32 * z, fx32 * w)
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

static void calculateVtx(float * x, float * y, float * z, float * w)
{
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
    G3SIM_SetCurColor(r, g, b);

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
			/* Five bits of alpha into eight. Multiplying by eight stops at 248, so a texel the
			 * game meant to be solid was drawn at 97% and let what was behind it show through;
			 * repeating the top bits reaches 255. */
			out[texOffset] = (u8)((alpha << 3) | (alpha >> 2));
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
			/* Five bits of alpha into eight. Multiplying by eight stops at 248, so a texel the
			 * game meant to be solid was drawn at 97% and let what was behind it show through;
			 * repeating the top bits reaches 255. */
			out[texOffset] = (u8)((alpha << 3) | (alpha >> 2));
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
    s_lightCacheDirty = 1;
    u8 diffuseRed = (data & 0b11111);
    u8 diffuseGreen = (data & 0b1111100000) >> 5;
    u8 diffuseBlue = (data & 0b111110000000000) >> 10;
    u8 setVtxColor = (data & 0b1000000000000000) >> 15;
    u8 ambientRed = (data & 0b111110000000000000000) >> 16;
    u8 ambientGreen = (data & 0b11111000000000000000000000) >> 21;
    u8 ambientBlue = (data & 0b1111100000000000000000000000000) >> 26;

    if(setVtxColor) {
        G3SIM_SetCurColor(diffuseRed << 3, diffuseGreen << 3, diffuseBlue << 3);
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
    s_lightCacheDirty = 1;
    u8 lightRed = (data & 0b11111);
    u8 lightGreen = (data & 0b1111100000) >> 5;
    u8 lightBlue = (data & 0b111110000000000) >> 10;
    u8 lightNum = (data & 0b11000000000000000000000000000000) >> 30;

    // Should get us close but colors might not be 100% accurate
    s_G3LightColor[lightNum][0] = lightRed << 3;
    s_G3LightColor[lightNum][1] = lightGreen << 3;
    s_G3LightColor[lightNum][2] = lightBlue << 3;
}

/* One of the geometry engine's 10-bit signed fixed-point components: a normal, or a light's
 * direction. Two's complement, nine fractional bits, so -1.0 to +0.998 in steps of 1/512.
 *
 * This was read into a u8. Three of them are packed into one command word and each was masked to
 * ten bits and then assigned to an eight-bit variable, which threw away bit 9 -- the sign -- before
 * the next line looked for it, and wrapped what was left of the magnitude modulo 256. So every
 * normal and every light direction came out positive and of the wrong length, the diffuse dot
 * product with it was meaningless, and clamping that at zero left most surfaces with no diffuse
 * light on them at all.
 *
 * A material whose colour lives in its diffuse term is then emission plus ambient, which for the
 * overworld's materials is black. Geometry that sets its colour outright with a COLOR command --
 * interface, billboards, anything unlit -- was unaffected, so the screen came out black with a few
 * textures still in it. That is the whole of the symptom.
 *
 * The old code also treated the field as sign-and-magnitude over 511. It is not: it is two's
 * complement over 512, which is what this does.
 */
static inline float G3SIM_Decode10(u32 packed)
{
    s32 v = (s32)(packed & 0x3FF);

    if (v & 0x200) {
        v -= 0x400;
    }
    return (float)v / 512.0f;
}

void G3SIM_LightVector(u32 data)
{
    s_lightCacheDirty = 1;
    u8 lightNum = (data & 0b11000000000000000000000000000000) >> 30;

    s_G3LightVector[lightNum][0] = G3SIM_Decode10(data);
    s_G3LightVector[lightNum][1] = G3SIM_Decode10(data >> 10);
    s_G3LightVector[lightNum][2] = G3SIM_Decode10(data >> 20);
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

//CurrentMatrix = CurrentMatrix * mtxs
void G3SIM_MtxMult33(MtxFx33 * mtx)
{
    G3SIM_Matrix_t mtxTemp;
    G3SIM_Matrix_t * currentMatrix;
    G3SIM_Matrix_t currentMatrixCopy;
    fx32 curSum;
    currentMatrix = getCurrentMatrix();
    mtxTemp.nums[0][0] = mtx->_00;
    mtxTemp.nums[0][1] = mtx->_01;
    mtxTemp.nums[0][2] = mtx->_02;
    mtxTemp.nums[0][3] = 0;
    mtxTemp.nums[1][0] = mtx->_10;
    mtxTemp.nums[1][1] = mtx->_11;
    mtxTemp.nums[1][2] = mtx->_12;
    mtxTemp.nums[1][3] = 0;
    mtxTemp.nums[2][0] = mtx->_20;
    mtxTemp.nums[2][1] = mtx->_21;
    mtxTemp.nums[2][2] = mtx->_22;
    mtxTemp.nums[2][3] = 0;
    mtxTemp.nums[3][0] = 0;
    mtxTemp.nums[3][1] = 0;
    mtxTemp.nums[3][2] = 0;
    mtxTemp.nums[3][3] = FX32_ONE;
    memcpy( &currentMatrixCopy, currentMatrix, sizeof(G3SIM_Matrix_t));

    //Do the multiplication
    for( int i=0; i < 3; i++ )
    {
        for(int j=0; j < 3; j++)
        {
            curSum = 0;
            for( int k=0; k < 3; k++ )
            {
                curSum = curSum + (FX_Mul(mtxTemp.nums[i][k], currentMatrixCopy.nums[k][j]));
            }
            currentMatrix->nums[i][j] = curSum;
            if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR )
            {
                currentPositionMatrix.nums[i][j] = curSum;
            }
        }
    }

    if( s_curMtxMode != GX_MTXMODE_TEXTURE )
    {
        calculateClipMatrix();
    }

    return;
}

//CurrentMatrix = CurrentMatrix * mtxs
void G3SIM_MtxMult43(MtxFx43 * mtx)
{
    G3SIM_Matrix_t mtxTemp;
    G3SIM_Matrix_t * currentMatrix;
    G3SIM_Matrix_t currentMatrixCopy;
    fx32 curSum;
    currentMatrix = getCurrentMatrix();
    mtxTemp.nums[0][0] = mtx->_00;
    mtxTemp.nums[0][1] = mtx->_01;
    mtxTemp.nums[0][2] = mtx->_02;
    mtxTemp.nums[0][3] = 0;
    mtxTemp.nums[1][0] = mtx->_10;
    mtxTemp.nums[1][1] = mtx->_11;
    mtxTemp.nums[1][2] = mtx->_12;
    mtxTemp.nums[1][3] = 0;
    mtxTemp.nums[2][0] = mtx->_20;
    mtxTemp.nums[2][1] = mtx->_21;
    mtxTemp.nums[2][2] = mtx->_22;
    mtxTemp.nums[2][3] = 0;
    mtxTemp.nums[3][0] = mtx->_30;
    mtxTemp.nums[3][1] = mtx->_31;
    mtxTemp.nums[3][2] = mtx->_32;
    mtxTemp.nums[3][3] = FX32_ONE;
    memcpy( &currentMatrixCopy, currentMatrix, sizeof(G3SIM_Matrix_t));

    //Do the multiplication
    for( int i=0; i < 4; i++ )
    {
        for(int j=0; j < 3; j++)
        {
            curSum = 0;
            for( int k=0; k < 4; k++ )
            {
                curSum = curSum + FX_Mul(mtxTemp.nums[i][k], currentMatrixCopy.nums[k][j]);
            }
            currentMatrix->nums[i][j] = curSum;
            if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR )
            {
                currentPositionMatrix.nums[i][j] = curSum;
            }
        }
    }

    if( s_curMtxMode != GX_MTXMODE_TEXTURE )
    {
        calculateClipMatrix();
    }
    return;
}

void G3SIM_MtxMult44(MtxFx44 * mtx)
{

    G3SIM_Matrix_t mtxTemp;
    G3SIM_Matrix_t * currentMatrix;
    G3SIM_Matrix_t currentMatrixCopy;
    fx32 curSum;
    currentMatrix = getCurrentMatrix();
    mtxTemp.nums[0][0] = mtx->_00;
    mtxTemp.nums[0][1] = mtx->_01;
    mtxTemp.nums[0][2] = mtx->_02;
    mtxTemp.nums[0][3] = mtx->_03;
    mtxTemp.nums[1][0] = mtx->_10;
    mtxTemp.nums[1][1] = mtx->_11;
    mtxTemp.nums[1][2] = mtx->_12;
    mtxTemp.nums[1][3] = mtx->_13;
    mtxTemp.nums[2][0] = mtx->_20;
    mtxTemp.nums[2][1] = mtx->_21;
    mtxTemp.nums[2][2] = mtx->_22;
    mtxTemp.nums[2][3] = mtx->_23;
    mtxTemp.nums[3][0] = mtx->_30;
    mtxTemp.nums[3][1] = mtx->_31;
    mtxTemp.nums[3][2] = mtx->_32;
    mtxTemp.nums[3][3] = mtx->_33;
    memcpy( &currentMatrixCopy, currentMatrix, sizeof(G3SIM_Matrix_t));

    //Do the multiplication
    for( int i=0; i < 4; i++ )
    {
        for(int j=0; j < 4; j++)
        {
            curSum = 0;
            for( int k=0; k < 4; k++ )
            {
                curSum = curSum + FX_Mul(mtxTemp.nums[i][k], currentMatrixCopy.nums[k][j]);
            }
            currentMatrix->nums[i][j] = curSum;
            if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR )
            {
                currentPositionMatrix.nums[i][j] = curSum;
            }
        }
    }

    if( s_curMtxMode != GX_MTXMODE_TEXTURE )
    {
        calculateClipMatrix();
    }
    return;
}

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
        memcpy( &currentPositionMatrix, &curMtxStack->mtxs[curMtxStack->stackPtr], sizeof(G3SIM_Matrix_t));
        s_lightCacheDirty = 1;
        memcpy( &currentVectorMatrix, &curMtxStack->mtxs[curMtxStack->stackPtr], sizeof(G3SIM_Matrix_t));
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
        s_lightCacheDirty = 1;
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
    fx32 s[3];

    s[0] = scale[0];
    s[1] = scale[1];
    s[2] = scale[2];
    
    G3SIM_Matrix_t * currentMatrix = getCurrentMatrix();

    for(int i=0; i < 3; i ++)
    {
        for( int j=0; j < 3; j ++)
        {
            currentMatrix->nums[i][j] = FX_Mul(s[i], currentMatrix->nums[i][j]);
            if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR )
            {
                //Also multiply the position matrix
                currentPositionMatrix.nums[i][j] = FX_Mul(s[i], currentPositionMatrix.nums[i][j]);
            }
        }   
    }
    if( s_curMtxMode != GX_MTXMODE_TEXTURE )
    {
        calculateClipMatrix();
    }
    return;
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
    fx32 s[3];

    s[0] = trans[0];
    s[1] = trans[1];
    s[2] = trans[2];
    G3SIM_Matrix_t * matrix = getCurrentMatrix();
    if( s_curMtxMode == GX_MTXMODE_POSITION_VECTOR )
    {
        currentPositionMatrix.nums[3][0] += (FX_Mul(s[0],currentPositionMatrix.nums[0][0]) + FX_Mul(s[1],currentPositionMatrix.nums[1][0]) + FX_Mul(s[2],currentPositionMatrix.nums[2][0]));
        currentPositionMatrix.nums[3][1] += (FX_Mul(s[0],currentPositionMatrix.nums[0][1]) + FX_Mul(s[1],currentPositionMatrix.nums[1][1]) + FX_Mul(s[2],currentPositionMatrix.nums[2][1]));
        currentPositionMatrix.nums[3][2] += (FX_Mul(s[0],currentPositionMatrix.nums[0][2]) + FX_Mul(s[1],currentPositionMatrix.nums[1][2]) + FX_Mul(s[2],currentPositionMatrix.nums[2][2]));
        currentPositionMatrix.nums[3][3] += (FX_Mul(s[0],currentPositionMatrix.nums[0][3]) + FX_Mul(s[1],currentPositionMatrix.nums[1][3]) + FX_Mul(s[2],currentPositionMatrix.nums[2][3]));
    }
    /* Each component into its own column. All four of these wrote [3][1], so a translation in
     * position mode moved the model in Y by the sum of the four dot products and never in X or Z --
     * which is a model assembled out of pieces that are all in the wrong place. The block above,
     * which does the same thing to the position matrix in position-and-vector mode, has the indices
     * right and is what this should have looked like. */
    matrix->nums[3][0] += (FX_Mul(s[0],matrix->nums[0][0]) + FX_Mul(s[1],matrix->nums[1][0]) + FX_Mul(s[2],matrix->nums[2][0]));
    matrix->nums[3][1] += (FX_Mul(s[0],matrix->nums[0][1]) + FX_Mul(s[1],matrix->nums[1][1]) + FX_Mul(s[2],matrix->nums[2][1]));
    matrix->nums[3][2] += (FX_Mul(s[0],matrix->nums[0][2]) + FX_Mul(s[1],matrix->nums[1][2]) + FX_Mul(s[2],matrix->nums[2][2]));
    matrix->nums[3][3] += (FX_Mul(s[0],matrix->nums[0][3]) + FX_Mul(s[1],matrix->nums[1][3]) + FX_Mul(s[2],matrix->nums[2][3]));

    if( s_curMtxMode != GX_MTXMODE_TEXTURE )
    {
        calculateClipMatrix();
    }
}

/* ---------------------------------------------------------------- the lighting cache
 *
 * G3SIM_Normal runs once per vertex -- fourteen thousand times a frame in the field -- and every
 * time it rebuilt the same things from scratch: the vector matrix converted from fixed point to
 * float (nine conversions, then nine more per enabled light), each light's direction multiplied
 * through that matrix, and every material-times-light colour product. None of those depend on the
 * vertex. They depend on the vector matrix, the lights and the material, all of which change once
 * per object or per material rather than per vertex.
 *
 * They are computed here instead, when something they depend on has changed. The dirty flag is set
 * by every writer of those things, and by getCurrentMatrix(), which every matrix command goes
 * through -- a flag set when nothing actually changed only costs one rebuild.
 */
static float s_vecMtxF[3][3];          /* the vector matrix's rotation part, in float */
static float s_lightDirF[4][3];        /* each light's direction through that matrix */
static float s_lightConst[4][3];       /* specular + ambient: what a light adds whatever the normal */
static float s_lightDiffuse[4][3];     /* what it adds per unit of diffuse level */

/* Out of line on purpose: it runs once per object, not once per vertex, and inlining it put its
 * whole body in the middle of the path that runs fourteen thousand times a frame. */
static __attribute__((noinline)) void G3SIM_RebuildLightCache(void)
{
    s_lightCacheDirty = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            s_vecMtxF[i][j] = FX_FX32_TO_F32(currentVectorMatrix.nums[i][j]);

    for (int n = 0; n < 4; n++) {
        for (int j = 0; j < 3; j++)
            s_lightDirF[n][j] = (s_G3LightVector[n][0] * s_vecMtxF[0][j])
                              + (s_G3LightVector[n][1] * s_vecMtxF[1][j])
                              + (s_G3LightVector[n][2] * s_vecMtxF[2][j]);
        for (int c = 0; c < 3; c++) {
            float lightC = (float)s_G3LightColor[n][c] * (1.0f / 255.0f);
            /* Specular is still missing its shininess term, as it was before; keeping it in the
             * constant part changes nothing about what it produces. */
            s_lightConst[n][c] = ((float)s_g3SpecularColor[c] * (1.0f / 255.0f)) * lightC
                               + ((float)s_g3AmbientColor[c] * (1.0f / 255.0f)) * lightC;
            s_lightDiffuse[n][c] = ((float)s_g3DiffuseColor[c] * (1.0f / 255.0f)) * lightC;
        }
    }
}

void G3SIM_Normal(u32 data)
{
    /* The same ten-bit signed components as a light's direction; see G3SIM_Decode10. */
    float normalVectorIn[3];

    g3NormalCalls++;

    /* No light enabled: the normal is not used for anything, and the colour is the emission colour
     * on its own. The field sends normals for these polygons anyway, and transforming a vector
     * nothing reads is the cheapest work to stop doing. */
    if ((s_curPolygonAttr.lightFlag & 0xF) == 0) {
        G3SIM_SetCurColor(s_g3EmissionColor[0], s_g3EmissionColor[1], s_g3EmissionColor[2]);
        return;
    }

    if (s_lightCacheDirty)
        G3SIM_RebuildLightCache();

    normalVectorIn[0] = G3SIM_Decode10(data);
    normalVectorIn[1] = G3SIM_Decode10(data >> 10);
    normalVectorIn[2] = G3SIM_Decode10(data >> 20);

    //Multiply by the vector matrix
    float normalVector[3];
    normalVector[0] = (normalVectorIn[0] * s_vecMtxF[0][0])
                    + (normalVectorIn[1] * s_vecMtxF[1][0])
                    + (normalVectorIn[2] * s_vecMtxF[2][0]);

    normalVector[1] = (normalVectorIn[0] * s_vecMtxF[0][1])
                    + (normalVectorIn[1] * s_vecMtxF[1][1])
                    + (normalVectorIn[2] * s_vecMtxF[2][1]);

    normalVector[2] = (normalVectorIn[0] * s_vecMtxF[0][2])
                    + (normalVectorIn[1] * s_vecMtxF[1][2])
                    + (normalVectorIn[2] * s_vecMtxF[2][2]);

    // Calculate the color

    float color[3];

    color[0] = (float)s_g3EmissionColor[0] * (1.0f / 255.0f);
    color[1] = (float)s_g3EmissionColor[1] * (1.0f / 255.0f);
    color[2] = (float)s_g3EmissionColor[2] * (1.0f / 255.0f);

    for(int lightNum=0; lightNum<4; lightNum++) {
        if((s_curPolygonAttr.lightFlag >> lightNum) & 1)
        {
            // Light is enabled

            //Calculate Diffuse level. The light's direction has already been through the vector
            //matrix; see the cache above.
            float diffuseLevel = (s_lightDirF[lightNum][0] * normalVector[0])
                               + (s_lightDirF[lightNum][1] * normalVector[1])
                               + (s_lightDirF[lightNum][2] * normalVector[2]);

            diffuseLevel = diffuseLevel * -1.0f;
            if(diffuseLevel < 0.0f) {
                diffuseLevel = 0.0f;
            }

            /* Specular and ambient do not depend on the normal, so they are one cached term; the
             * diffuse coefficient is the other, scaled by the level just computed. */
            color[0] = color[0] + s_lightConst[lightNum][0] + s_lightDiffuse[lightNum][0] * diffuseLevel;
            color[1] = color[1] + s_lightConst[lightNum][1] + s_lightDiffuse[lightNum][1] * diffuseLevel;
            color[2] = color[2] + s_lightConst[lightNum][2] + s_lightDiffuse[lightNum][2] * diffuseLevel;
        }
    }

    if(color[0] > 1.0f) {
        color[0] = 1.0f;
    }
    if(color[1] > 1.0f) {
        color[1] = 1.0f;
    }
    if(color[2] > 1.0f) {
        color[2] = 1.0f;
    }

    /* Through the byte, deliberately. Taking the float straight across would be finer than what
     * this produced before, and the point here is to do the conversion once per normal instead of
     * once per vertex -- not to change what comes out. */
    G3SIM_SetCurColor((u8)(color[0] * 255.0f), (u8)(color[1] * 255.0f), (u8)(color[2] * 255.0f));
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
    /* Bit 11: whether a translucent pixel updates the depth buffer. The DS leaves it off by
     * default, which is why a translucent polygon on hardware does not hide the translucent
     * polygons drawn after it. The field declares it and this decoder was dropping it. */
    u8 transDepth = (data & 0b100000000000) >> 11;

    if(cullMode != s_curPolygonAttr.cullMode || polygonMode != s_curPolygonAttr.polygonMode || alpha != s_curPolygonAttr.alphaInt || depthTest != s_curPolygonAttr.depthTest || useFog != s_curPolygonAttr.fogEnable || transDepth != s_curPolygonAttr.translucentDepth)
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
    s_curPolygonAttr.translucentDepth = transDepth;

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
    s_lightCacheDirty = 1;
    u8 specularRed = (data & 0b11111);
    u8 specularGreen = (data & 0b1111100000) >> 5;
    u8 specularBlue = (data & 0b111110000000000) >> 10;
    u8 setVtxColor = (data & 0b1000000000000000) >> 15;
    u8 emissionRed = (data & 0b111110000000000000000) >> 16;
    u8 emissionGreen = (data & 0b11111000000000000000000000) >> 21;
    u8 emissionBlue = (data & 0b1111100000000000000000000000000) >> 26;

    if(setVtxColor) {
        G3SIM_SetCurColor(specularRed << 3, specularGreen << 3, specularBlue << 3);
    }
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
    /* The reciprocals the vertex path wants. Computed here, where the size changes, rather than
     * twice per vertex. */
    s_texInvSSize = s_texImageParam.textureSSize ? 1.0f / (float)s_texImageParam.textureSSize : 0.0f;
    s_texInvTSize = s_texImageParam.textureTSize ? 1.0f / (float)s_texImageParam.textureTSize : 0.0f;
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

//Add a vertex
void G3SIM_Vtx(s16 x, s16 y, s16 z)
{
    g3VtxCalls++;
    fx32 fxX;
    fx32 fxY;
    fx32 fxZ;
    fx32 fxW;

    fxX = x;
    fxY = y;
    fxZ = z;
    fxW = FX32_ONE;

    calculateVtxFx(&fxX, &fxY, &fxZ, &fxW);

    s_g3PolygonVerts[s_G3numInPoly].x = fxX;
    s_g3PolygonVerts[s_G3numInPoly].y = fxY;
    s_g3PolygonVerts[s_G3numInPoly].z = fxZ;
    s_g3PolygonVerts[s_G3numInPoly].w = fxW;

    s_g3PolygonVerts[s_G3numInPoly].r = s_g3CurColorF[0];
    s_g3PolygonVerts[s_G3numInPoly].g = s_g3CurColorF[1];
    s_g3PolygonVerts[s_G3numInPoly].b = s_g3CurColorF[2];
    s_g3PolygonVerts[s_G3numInPoly].a = s_curPolygonAttr.alpha;

    s_g3PolygonVerts[s_G3numInPoly].s = s_g3NextTexCoordS * s_texInvSSize;
    s_g3PolygonVerts[s_G3numInPoly].t = s_g3NextTexCoordT * s_texInvTSize;

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

/* Polygons the simulator threw away rather than handed to the renderer, since the two reasons it
 * has for doing so are both approximations and both have been wrong. A scene that looks like it is
 * missing geometry is either dropping it here or never producing it, and the log should say which.
 *
 * droppedW: a vertex at W = 0, which is a polygon edge-on to the camera and genuinely undrawable.
 * droppedFar: a vertex that came out of the perspective divide far outside the screen, which is
 * this simulator's stand-in for the near-plane clipping it does not do -- a polygon crossing the
 * camera plane produces one, and a real DS would have cut the polygon rather than dropped it. */
extern "C" { extern unsigned g3DroppedW, g3DroppedFar; }

void G3SIM_SubmitPolygon(G3SIM_FxVtx_t * fxVerts, int numVerts) {
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
    
        /* One reciprocal for both axes, and the two constant divides folded into it. Each of these
         * was a separate float division on a path that runs fourteen thousand times a frame, and
         * only one of the divisors is a power of two, so the compiler had to keep the rest. */
        float invW = 1.0f / (2.0f * floatW);

        screenX = ((floatX + floatW) * (256.0f / 128.0f)) * invW - 1.0f;
        screenY = ((floatY + floatW) * (192.0f / 96.0f)) * invW - 1.0f;
    
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
    //
    // numVerts, not 4, and this is not a tidy-up. A triangle has three vertices; s_g3PolygonVerts,
    // which every caller passes, is a four-element global. The fourth slot a triangle never writes
    // holds whatever the last quad left there -- and, until a quad has been drawn at all, the zero
    // it was initialised with. So "this polygon has a vertex at W = 0, throw it away" was being
    // decided by a vertex the polygon does not have, and on a zero it threw away every triangle.
    //
    // On the console that is the overworld drawing its quads and almost nothing else: the ground
    // and the walls, which are quads, with the models on top of them -- the player, the people, the
    // props, all triangle strips -- missing, and a stale W in that fourth slot deciding from one
    // scene to the next which of them came back. It reads as a screen that is mostly black with a
    // few textures in it.
    //
    // wsize is computed in the same loop and so changes with it, which is also the correction: a
    // triangle's W scale has no business being set by a vertex from the quad before it.
    u32 wsize = 0;
    for(int i=0; i < numVerts; i++) {
        G3SIM_FxVtx_t * curVtx = &fxVerts[i];
        u32 w = curVtx->w;
        if(w == 0){
            if (addPolygon) g3DroppedW++;
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
            if (addPolygon) g3DroppedFar++;
            addPolygon = FALSE;
        }
        if(glVerts[i].y > 50.0f || glVerts[i].y < -50.0f) {
            if (addPolygon) g3DroppedFar++;
            addPolygon = FALSE;
        }
    }

    if(addPolygon) {
        for(int i=0; i < numFinalVerts; i++) {
            G3SIM_AddVtx(&glVerts[i]);
        }
    }


}
