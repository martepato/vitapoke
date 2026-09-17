#include <cstdlib>
#include <cstring>
#include <simulator/g3_handler.h>
#include <simulator/g3_draw.h>
#include <simulator/drawmsg.h>
extern "C" unsigned s_numG3DrawsThisFrame; unsigned s_numG3DrawsThisFrame=0;
static draw_command_type_t DecodeG3Op(u32 op) {
  switch (op) {
  case G3OP_NOP:
    return DRAW_CMD_G3_NOP;
  case G3OP_MTX_MODE:
    return DRAW_CMD_G3_MTXMODE;
  case G3OP_MTX_PUSH:
    return DRAW_CMD_G3_PUSHMTX;
  case G3OP_MTX_POP:
    return DRAW_CMD_G3_POPMTX;
  case G3OP_MTX_STORE:
    return DRAW_CMD_G3_STOREMTX;
  case G3OP_MTX_RESTORE:
    return DRAW_CMD_G3_RESTOREMTX;
  case G3OP_MTX_IDENTITY:
    return DRAW_CMD_G3_IDENTITY;
  case G3OP_MTX_LOAD_4x4:
    return DRAW_CMD_G3_LOADMTX44;
  case G3OP_MTX_LOAD_4x3:
    return DRAW_CMD_G3_LOADMTX43;
  case G3OP_MTX_MULT_4x4:
    return DRAW_CMD_G3_MTXMULT44;
  case G3OP_MTX_MULT_4x3:
    return DRAW_CMD_G3_MTXMULT43;
  case G3OP_MTX_MULT_3x3:
    return DRAW_CMD_G3_MTXMULT33;
  case G3OP_MTX_SCALE:
    return DRAW_CMD_G3_SCALE;
  case G3OP_MTX_TRANS:
    return DRAW_CMD_G3_TRANSLATE;
  case G3OP_COLOR:
    return DRAW_CMD_G3_COLOR;
  case G3OP_NORMAL:
    return DRAW_CMD_G3_NORMAL;
  case G3OP_TEXCOORD:
    return DRAW_CMD_G3_TEXCOORD;
  case G3OP_VTX_16:
    return DRAW_CMD_G3_VTX16;
  case G3OP_VTX_10:
    return DRAW_CMD_G3_VTX10;
  case G3OP_VTX_XY:
    return DRAW_CMD_G3_VTXXY;
  case G3OP_VTX_XZ:
    return DRAW_CMD_G3_VTXXZ;
  case G3OP_VTX_YZ:
    return DRAW_CMD_G3_VTXYZ;
  case G3OP_VTX_DIFF:
    return DRAW_CMD_G3_VTXDIFF;
  case G3OP_POLYGON_ATTR:
    return DRAW_CMD_G3_POLYGONATTR;
  case G3OP_TEXIMAGE_PARAM:
    return DRAW_CMD_G3_TEXIMAGEPARAM;
  case G3OP_TEXPLTT_BASE:
    return DRAW_CMD_G3_TEXPLTTBASE;
  case G3OP_DIF_AMB:
    return DRAW_CMD_G3_DIFF_AMB;
  case G3OP_SPE_EMI:
    return DRAW_CMD_G3_SPEC_EMI;
  case G3OP_LIGHT_VECTOR:
    return DRAW_CMD_G3_LIGHTVECTOR;
  case G3OP_LIGHT_COLOR:
    return DRAW_CMD_G3_LIGHTCOLOR;
  case G3OP_SHININESS:
    return DRAW_CMD_G3_SHININESS;
  case G3OP_BEGIN:
    return DRAW_CMD_G3_BEGIN;
  case G3OP_END:
    return DRAW_CMD_G3_END;
  case G3OP_SWAP_BUFFERS:
    return DRAW_CMD_G3_SWAPBUFFERS;
  case G3OP_VIEWPORT:
    return DRAW_CMD_G3_VIEWPORT;
  case G3OP_BOX_TEST:
    return DRAW_CMD_G3_BOXTEST;
  case G3OP_POS_TEST:
    return DRAW_CMD_G3_POSITIONTEST;
  case G3OP_VEC_TEST:
    return DRAW_CMD_G3_VECTORTEST;
  case G3OP_DUMMY_COMMAND:
    return DRAW_CMD_G3_DUMMY;
  default:
    return DRAW_CMD_CNT;
  }
}

static void ConvertMsgParams(draw_msg_t *msg) {
  // Conversions compact widened parameters in ascending order.
  // Each destination ends before the next unread 8-byte source slot.
  switch (msg->type) {
  // TODO Commands
  case DRAW_CMD_G3_VIEWPORT:
    break;

  case DRAW_CMD_G3_BOXTEST:
    msg->data.boxtest.x = (s16)msg->data.numsS64[1];
    msg->data.boxtest.y = (s16)msg->data.numsS64[2];
    msg->data.boxtest.z = (s16)msg->data.numsS64[3];
    msg->data.boxtest.width = (s16)msg->data.numsS64[4];
    msg->data.boxtest.height = (s16)msg->data.numsS64[5];
    msg->data.boxtest.depth = (s16)msg->data.numsS64[6];
    break;
  case DRAW_CMD_G3_PUSHMTX:
  case DRAW_CMD_G3_END:
  case DRAW_CMD_G3_NOP:
    break;
  case DRAW_CMD_G3_MTXMODE:
  case DRAW_CMD_G3_POPMTX:
  case DRAW_CMD_G3_STOREMTX:
  case DRAW_CMD_G3_RESTOREMTX:
  case DRAW_CMD_G3_BEGIN:
    msg->data.numU8 = msg->data.numsU64[1];
    break;
  case DRAW_CMD_G3_COLOR:
    msg->data.numU16 = msg->data.numsU64[1];
    break;
  case DRAW_CMD_G3_VTX10:
  case DRAW_CMD_G3_VTXDIFF:
  case DRAW_CMD_G3_TEXIMAGEPARAM:
  case DRAW_CMD_G3_TEXPLTTBASE:
  case DRAW_CMD_G3_TEXCOORD:
  case DRAW_CMD_G3_POLYGONATTR:
  case DRAW_CMD_G3_LIGHTVECTOR:
  case DRAW_CMD_G3_DIFF_AMB:
  case DRAW_CMD_G3_NORMAL:
  case DRAW_CMD_G3_LIGHTCOLOR:
  case DRAW_CMD_G3_SPEC_EMI:
    msg->data.numU32 = msg->data.numsU64[1];
    break;
  case DRAW_CMD_G3_VTX:
  case DRAW_CMD_G3_VTX16:
    msg->data.xyz.x = (s16)msg->data.numsS64[1];
    msg->data.xyz.y = (s16)msg->data.numsS64[2];
    msg->data.xyz.z = (s16)msg->data.numsS64[3];
    break;
  case DRAW_CMD_G3_VTXXY:
    msg->data.xyz.x = (s16)msg->data.numsS64[1];
    msg->data.xyz.y = (s16)msg->data.numsS64[2];
    break;
  case DRAW_CMD_G3_VTXXZ:
    msg->data.xyz.x = (s16)msg->data.numsS64[1];
    msg->data.xyz.z = (s16)msg->data.numsS64[2];
    break;
  case DRAW_CMD_G3_VTXYZ:
    msg->data.xyz.y = (s16)msg->data.numsS64[1];
    msg->data.xyz.z = (s16)msg->data.numsS64[2];
    break;
  case DRAW_CMD_G3_SCALE:
  case DRAW_CMD_G3_TRANSLATE:
    msg->data.scale[0] = (s32)msg->data.numsS64[1];
    msg->data.scale[1] = (s32)msg->data.numsS64[2];
    msg->data.scale[2] = (s32)msg->data.numsS64[3];
    break;
  case DRAW_CMD_G3_MTXMULT33:
    msg->data.mtx33._00 = (s32)msg->data.numsS64[1];
    msg->data.mtx33._01 = (s32)msg->data.numsS64[2];
    msg->data.mtx33._02 = (s32)msg->data.numsS64[3];
    msg->data.mtx33._10 = (s32)msg->data.numsS64[4];
    msg->data.mtx33._11 = (s32)msg->data.numsS64[5];
    msg->data.mtx33._12 = (s32)msg->data.numsS64[6];
    msg->data.mtx33._20 = (s32)msg->data.numsS64[7];
    msg->data.mtx33._21 = (s32)msg->data.numsS64[8];
    msg->data.mtx33._22 = (s32)msg->data.numsS64[9];
    break;
  case DRAW_CMD_G3_LOADMTX43:
  case DRAW_CMD_G3_MTXMULT43:
    msg->data.mtx43._00 = (s32)msg->data.numsS64[1];
    msg->data.mtx43._01 = (s32)msg->data.numsS64[2];
    msg->data.mtx43._02 = (s32)msg->data.numsS64[3];
    msg->data.mtx43._10 = (s32)msg->data.numsS64[4];
    msg->data.mtx43._11 = (s32)msg->data.numsS64[5];
    msg->data.mtx43._12 = (s32)msg->data.numsS64[6];
    msg->data.mtx43._20 = (s32)msg->data.numsS64[7];
    msg->data.mtx43._21 = (s32)msg->data.numsS64[8];
    msg->data.mtx43._22 = (s32)msg->data.numsS64[9];
    msg->data.mtx43._30 = (s32)msg->data.numsS64[10];
    msg->data.mtx43._31 = (s32)msg->data.numsS64[11];
    msg->data.mtx43._32 = (s32)msg->data.numsS64[12];
    break;
  case DRAW_CMD_G3_LOADMTX44:
  case DRAW_CMD_G3_MTXMULT44:
    msg->data.mtx44._00 = (s32)msg->data.numsS64[1];
    msg->data.mtx44._01 = (s32)msg->data.numsS64[2];
    msg->data.mtx44._02 = (s32)msg->data.numsS64[3];
    msg->data.mtx44._03 = (s32)msg->data.numsS64[4];
    msg->data.mtx44._10 = (s32)msg->data.numsS64[5];
    msg->data.mtx44._11 = (s32)msg->data.numsS64[6];
    msg->data.mtx44._12 = (s32)msg->data.numsS64[7];
    msg->data.mtx44._13 = (s32)msg->data.numsS64[8];
    msg->data.mtx44._20 = (s32)msg->data.numsS64[9];
    msg->data.mtx44._21 = (s32)msg->data.numsS64[10];
    msg->data.mtx44._22 = (s32)msg->data.numsS64[11];
    msg->data.mtx44._23 = (s32)msg->data.numsS64[12];
    msg->data.mtx44._30 = (s32)msg->data.numsS64[13];
    msg->data.mtx44._31 = (s32)msg->data.numsS64[14];
    msg->data.mtx44._32 = (s32)msg->data.numsS64[15];
    msg->data.mtx44._33 = (s32)msg->data.numsS64[16];
    break;
  default:
    break;
  }
}

static u8 GetNumParamsFromCommandType(draw_command_type_t type) {
  switch (type) {
  case DRAW_CMD_G3_NOP:
  case DRAW_CMD_G3_END:
  case DRAW_CMD_G3_IDENTITY:
  case DRAW_CMD_G3_PUSHMTX:
  case DRAW_CMD_G3_DUMMY:
    return 0;
  case DRAW_CMD_G3_STOREMTX:
  case DRAW_CMD_G3_RESTOREMTX:
  case DRAW_CMD_G3_MTXMODE:
  case DRAW_CMD_G3_LIGHTVECTOR:
  case DRAW_CMD_G3_DIFF_AMB:
  case DRAW_CMD_G3_SPEC_EMI:
  case DRAW_CMD_G3_POLYGONATTR:
  case DRAW_CMD_G3_LIGHTCOLOR:
  case DRAW_CMD_G3_NORMAL:
  case DRAW_CMD_G3_VIEWPORT:
  case DRAW_CMD_G3_TEXIMAGEPARAM:
  case DRAW_CMD_G3_TEXPLTTBASE:
  case DRAW_CMD_G3_VTX10:
  case DRAW_CMD_G3_VTXDIFF:
  case DRAW_CMD_G3_BEGIN:
  case DRAW_CMD_G3_COLOR:
  case DRAW_CMD_G3_TEXCOORD:
  case DRAW_CMD_G3_SWAPBUFFERS:
    return 1;
  case DRAW_CMD_G3_VTXXY:
  case DRAW_CMD_G3_VTXXZ:
  case DRAW_CMD_G3_VTXYZ:
    return 2;
  case DRAW_CMD_G3_SCALE:
  case DRAW_CMD_G3_TRANSLATE:
    return 3;
  case DRAW_CMD_G3_VTX:
  case DRAW_CMD_G3_VTX16:
  case DRAW_CMD_G3_POPMTX:
    return 4;
  case DRAW_CMD_G3_BOXTEST:
    return 6;
  case DRAW_CMD_G3_MTXMULT33:
    return 9;
  case DRAW_CMD_G3_LOADMTX43:
  case DRAW_CMD_G3_MTXMULT43:
    return 12;
  case DRAW_CMD_G3_LOADMTX44:
  case DRAW_CMD_G3_MTXMULT44:
    return 16;
  default:
    return 0;
  }
}

static u8 GetParamSizeFromCommandType(draw_command_type_t type) {
  switch (type) {
  case DRAW_CMD_G3_NOP:
  case DRAW_CMD_G3_IDENTITY:
  case DRAW_CMD_G3_PUSHMTX:
  case DRAW_CMD_G3_DUMMY:
    return 0;
  case DRAW_CMD_G3_POPMTX:
    return 1;
  case DRAW_CMD_G3_VTX:
  case DRAW_CMD_G3_VTX16:
  case DRAW_CMD_G3_VTXXY:
  case DRAW_CMD_G3_VTXXZ:
  case DRAW_CMD_G3_VTXYZ:
  case DRAW_CMD_G3_BOXTEST:
    return 2;
  case DRAW_CMD_G3_LOADMTX43:
  case DRAW_CMD_G3_LOADMTX44:
  case DRAW_CMD_G3_MTXMULT33:
  case DRAW_CMD_G3_MTXMODE:
  case DRAW_CMD_G3_LIGHTVECTOR:
  case DRAW_CMD_G3_DIFF_AMB:
  case DRAW_CMD_G3_SPEC_EMI:
  case DRAW_CMD_G3_POLYGONATTR:
  case DRAW_CMD_G3_MTXMULT44:
  case DRAW_CMD_G3_LIGHTCOLOR:
  case DRAW_CMD_G3_MTXMULT43:
  case DRAW_CMD_G3_NORMAL:
  case DRAW_CMD_G3_VIEWPORT:
  case DRAW_CMD_G3_SCALE:
  case DRAW_CMD_G3_TRANSLATE:
  case DRAW_CMD_G3_STOREMTX:
  case DRAW_CMD_G3_RESTOREMTX:
  case DRAW_CMD_G3_TEXIMAGEPARAM:
  case DRAW_CMD_G3_TEXPLTTBASE:
  case DRAW_CMD_G3_VTX10:
  case DRAW_CMD_G3_VTXDIFF:
  case DRAW_CMD_G3_BEGIN:
  case DRAW_CMD_G3_COLOR:
  case DRAW_CMD_G3_TEXCOORD:
  case DRAW_CMD_G3_SWAPBUFFERS:
    return 4;
  default:
    return 0;
  }
}

static u8 isParamSigned(draw_command_type_t type) {
  switch (type) {
  case DRAW_CMD_G3_NOP:
  case DRAW_CMD_G3_BEGIN:
  case DRAW_CMD_G3_IDENTITY:
  case DRAW_CMD_G3_PUSHMTX:
  case DRAW_CMD_G3_MTXMODE:
  case DRAW_CMD_G3_LIGHTVECTOR:
  case DRAW_CMD_G3_DIFF_AMB:
  case DRAW_CMD_G3_SPEC_EMI:
  case DRAW_CMD_G3_POLYGONATTR:
  case DRAW_CMD_G3_LIGHTCOLOR:
  case DRAW_CMD_G3_DUMMY:
  case DRAW_CMD_G3_VIEWPORT:
  case DRAW_CMD_G3_STOREMTX:
  case DRAW_CMD_G3_RESTOREMTX:
  case DRAW_CMD_G3_TEXIMAGEPARAM:
  case DRAW_CMD_G3_TEXPLTTBASE:
  case DRAW_CMD_G3_VTX10:
  case DRAW_CMD_G3_VTXDIFF:
  case DRAW_CMD_G3_COLOR:
  case DRAW_CMD_G3_TEXCOORD:
  case DRAW_CMD_G3_SWAPBUFFERS:
    return 0;
  case DRAW_CMD_G3_VTX:
  case DRAW_CMD_G3_VTX16:
  case DRAW_CMD_G3_NORMAL:
  case DRAW_CMD_G3_LOADMTX43:
  case DRAW_CMD_G3_LOADMTX44:
  case DRAW_CMD_G3_MTXMULT44:
  case DRAW_CMD_G3_MTXMULT43:
  case DRAW_CMD_G3_SCALE:
  case DRAW_CMD_G3_TRANSLATE:
  case DRAW_CMD_G3_POPMTX:
  case DRAW_CMD_G3_VTXXY:
  case DRAW_CMD_G3_VTXXZ:
  case DRAW_CMD_G3_VTXYZ:
  case DRAW_CMD_G3_MTXMULT33:
  case DRAW_CMD_G3_BOXTEST:
    return 1;
  default:
    return 0;
  }
}

static xyz_s_t prevXYZ;

/* ================= Display-list geometry cache =================
   The overworld re-sends the same static model shape lists every frame (BEGIN/TEXCOORD/
   NORMAL/VTX.../END, no matrix or material commands) from the same addresses: ~9,000
   vertices and ~17 ms of CPU decode+lighting+conversion per frame in the emulator. Their
   GE output (object-space raw vertices, GE transform) depends only on the list bytes and a
   small entry state, so it is recorded once and replayed by memcpy while (a) the bytes at
   that address are unchanged (full memcmp, no hashing) and (b) the entry state matches.
   The exit primitive-slot state is restored too, so the result is exact by construction.
   Lists with any other command, any CPU-path polygon, or a mid-list flush are never cached. */
#include "g3_listcache.h"
#include <cstdlib>
#include <malloc.h>
#include <pspkernel.h>
#ifndef VITAPOKE_LIST_CACHE
#define VITAPOKE_LIST_CACHE 1
#endif
struct ListEntry{const void*ptr;unsigned size;u8*src;GuVertex*verts;unsigned nverts,polys,lastUse,skipUntil,deps,impureSize,impureSig;u8 mismatches,impure;G3GeomEntryState entry;G3SlotState exit;xyz_s_t prevIn,prevOut;};
/* signature of a list's first words: an 'uncacheable' mark is dropped when the bytes at that address change */
static inline unsigned ListSig(const void*p,unsigned size){const unsigned*w=(const unsigned*)p;unsigned n=size/4;if(n>8)n=8;unsigned s=size;for(unsigned i=0;i<n;i++)s=s*31u+w[i];return s;}
enum{kListSlots=512,kListMaxEntries=384,kListMaxBytes=1536*1024,kListMaxOne=64*1024};
static ListEntry listTable[kListSlots];static unsigned listEntries,listBytes,listFrame,listHighBytes;
static unsigned listHits,listRecords,listHitVerts,listVolatile,listBroken,listMissBytes,listMissState,listMissNoSrc;
/* Content-keyed cache (experiment): NNS scratch buffers send many different vertex lists through one address in a
   frame, so the address cache marks them volatile. Pure lists seen at a volatile address are stored and found by
   (size, hash of words) instead; a hit is still verified byte-for-byte and against the same entry-state deps. */
enum{kContentSlots=1024,kContentProbe=32,kContentMaxBytes=1024*1024};
static ListEntry contentTable[kContentSlots];static unsigned contentBytes,contentHits,contentStores;
static inline unsigned ContentHash(const void*p,unsigned size){const unsigned*w=(const unsigned*)p;unsigned h=2166136261u^size;for(unsigned i=0,n=size>>2;i<n;i++)h=(h^w[i])*16777619u;return h?h:1;}
extern "C" void VitaNativeG3ListCacheStats(unsigned*hits,unsigned*records,unsigned*entries,unsigned*bytes,unsigned*hitVerts,unsigned*highBytes,unsigned*vol){
 if(hits)*hits=listHits;if(records)*records=listRecords;if(entries)*entries=listEntries;if(bytes)*bytes=listBytes;if(hitVerts)*hitVerts=listHitVerts;if(highBytes)*highBytes=listHighBytes;if(vol)*vol=listVolatile|(listBroken<<16);
 listHits=listRecords=listHitVerts=0;}
static inline unsigned ListHash(const void*p){uintptr_t v=(uintptr_t)p;return (unsigned)((v>>2)*2654435761u)>>23;}   /* 9 bits */
enum{kListProbe=64};
static ListEntry*ListFind(const void*p){unsigned i=ListHash(p);for(unsigned n=0;n<kListProbe;n++){ListEntry*e=&listTable[(i+n)&(kListSlots-1)];if(e->ptr==p)return e;if(!e->ptr)return nullptr;}return nullptr;}
static void ListRelease(ListEntry*e){if(e->src){listBytes-=e->size+e->nverts*sizeof(GuVertex);listEntries--;}free(e->src);G3ListDeferFree(e->verts);e->src=nullptr;e->verts=nullptr;e->nverts=0;}
static void ContentRelease(ListEntry*e){if(e->src)contentBytes-=e->size+e->nverts*sizeof(GuVertex);free(e->src);G3ListDeferFree(e->verts);e->src=nullptr;e->verts=nullptr;e->nverts=0;}
static ListEntry*ContentFind(unsigned size,unsigned h){unsigned i=h&(kContentSlots-1);for(unsigned n=0;n<kContentProbe;n++){ListEntry*e=&contentTable[(i+n)&(kContentSlots-1)];if(e->src&&e->size==size&&e->impureSig==h)return e;}return nullptr;}
static ListEntry*ContentSlotFor(unsigned h){unsigned i=h&(kContentSlots-1);ListEntry*best=nullptr;for(unsigned n=0;n<kContentProbe;n++){ListEntry*e=&contentTable[(i+n)&(kContentSlots-1)];if(!e->src)return e;if(!best||e->lastUse<best->lastUse)best=e;}ContentRelease(best);return best;}
static void ContentMakeRoom(unsigned need){while(contentBytes+need>kContentMaxBytes){ListEntry*old=nullptr;for(unsigned i=0;i<kContentSlots;i++)if(contentTable[i].src&&(!old||contentTable[i].lastUse<old->lastUse))old=&contentTable[i];if(!old)break;ContentRelease(old);}}
/* Linear probing, bounded. Dead keys (released entries, uncacheable marks) idle for 120 frames
   are reused as tombstones so a table of reused buffer addresses never fills up. */
static ListEntry*ListSlotFor(const void*p){unsigned i=ListHash(p);ListEntry*tomb=nullptr;
 for(unsigned n=0;n<kListProbe;n++){ListEntry*e=&listTable[(i+n)&(kListSlots-1)];
  if(e->ptr==p)return e;
  if(!e->ptr){if(tomb)break;memset(e,0,sizeof*e);e->ptr=p;return e;}
  if(!tomb&&!e->src&&e->lastUse+120<listFrame)tomb=e;}
 if(tomb){memset(tomb,0,sizeof*tomb);tomb->ptr=p;return tomb;}return nullptr;}
static void ListEvictOne(){ListEntry*victim=nullptr;for(unsigned i=0;i<kListSlots;i++){ListEntry*e=&listTable[i];if(e->src&&(!victim||e->lastUse<victim->lastUse))victim=e;}if(victim)ListRelease(victim);}
static void ListMakeRoom(unsigned need){while(listEntries&&(listEntries>=kListMaxEntries||listBytes+need>kListMaxBytes))ListEvictOne();}
extern "C" void G3SIM_VtxAssemble(s16,s16,s16);

#include <pspkernel.h>
unsigned g3ProfUs=0,g3ProfCalls=0;
extern "C" void VitaNativeG3GetProfile(unsigned*us,unsigned*calls){if(us)*us=g3ProfUs;if(calls)*calls=g3ProfCalls;g3ProfUs=0;g3ProfCalls=0;}

#ifdef VITAPOKE_G3_STATS
/* Diagnostic only (never in a release build): per-frame command histogram and display-list
   repetition statistics, printed every 30 SWAPBUFFERS. */
#include <cstdio>
static unsigned g3StatList[64],g3StatImm[64],g3StatFrames,g3StatLists,g3StatBytes,g3StatSame,g3StatChanged,g3StatNew;
unsigned g3CatUs[4],g3CatN[4],g3OpUs[64],g3OpN[64],g3HitUs[4];   /* hit sub-steps: 0 compare,1 state,2 draw,3 restore */extern "C" unsigned g3DrawUs,g3DrawN;   /* 0 hit,1 record,2 plain,3 immediate */
struct G3ListRec{const void*ptr;unsigned size,hash;};
static G3ListRec g3ListsA[2048],g3ListsB[2048];static G3ListRec*g3Prev=g3ListsA,*g3Cur=g3ListsB;static unsigned g3PrevN,g3CurN;
static u8 GetNumParamsFromCommandType(draw_command_type_t);static u8 GetParamSizeFromCommandType(draw_command_type_t);
static const char*const g3OpName[48]={"nop","D0","D1","D2","D3","MODE","PUSH","POP","STORE","RESTORE","IDENT","SCALE","TRANS","LOAD44","LOAD43","MUL33","MUL43","MUL44","COLOR","N","T","VTX","V10","V16","VXY","VXZ","VYZ","VDIFF","PATTR","TEXP","TPLT","MDA","MSE","LVEC","LCOL","BEGIN","END","SWAP","VIEW","BOX","POS","VEC","DIFAMB","SPEEMI","SHIN","DUMMY","LIST","?"};
static void G3StatsDump(const void*ptr,unsigned size,unsigned hash){
 char line[700];int n=snprintf(line,sizeof line,"[G3-DUMP] f=%u ptr=%p size=%u hash=%08x ops=",g3StatFrames,ptr,size,hash);
 const unsigned char*p=(const unsigned char*)ptr;unsigned off=0;int last=-1,run=0;
 auto emit=[&](){if(last<0)return;if(n<(int)sizeof line-40){if(run>1)n+=snprintf(line+n,sizeof line-n,"%s*%d ",g3OpName[last],run);else n+=snprintf(line+n,sizeof line-n,"%s ",g3OpName[last]);}};
 while(off+4<=size){unsigned w=*(const unsigned*)(p+off);off+=4;
  for(int i=0;i<4;i++){unsigned op=(w>>(i*8))&255;draw_command_type_t t=DecodeG3Op(op);int ti=t>47?47:(int)t;
   if(ti==last)run++;else{emit();last=ti;run=1;}
   int np=GetNumParamsFromCommandType(t),ps=GetParamSizeFromCommandType(t);
   /* packed param bytes: VTX16 (2 params of 2 bytes), matrices etc.: use the decoder's own param sizes */
   if(t==DRAW_CMD_G3_VTX16||t==DRAW_CMD_G3_VTX)off+=8;
   else if(t==DRAW_CMD_G3_VTXXY||t==DRAW_CMD_G3_VTXXZ||t==DRAW_CMD_G3_VTXYZ)off+=4;
   else off+=np*ps;
  }}
 emit();printf("%s\n",line);
}
static void G3StatsList(const void*ptr,unsigned size){
#ifndef VITAPOKE_G3_STATS_LISTS
 g3StatLists++;g3StatBytes+=size;return;
#endif
 unsigned h=2166136261u;const unsigned*w=(const unsigned*)ptr;for(unsigned i=0;i<size/4;i++)h=(h^w[i])*16777619u;
 g3StatLists++;g3StatBytes+=size;bool found=false;
 for(unsigned i=0;i<g3PrevN;i++)if(g3Prev[i].ptr==ptr&&g3Prev[i].size==size){found=true;if(g3Prev[i].hash==h)g3StatSame++;else g3StatChanged++;break;}
 if(!found)g3StatNew++;
 if(g3CurN<2048)g3Cur[g3CurN++]={ptr,size,h};
 if(g3StatFrames==1200||g3StatFrames==1201)G3StatsDump(ptr,size,h);
}
static void G3StatsSwap(){
 G3ListRec*t=g3Prev;g3Prev=g3Cur;g3Cur=t;g3PrevN=g3CurN;g3CurN=0;
 if(++g3StatFrames%30)return;
 printf("[G3-STATS] content: hits/frame=%u stores/frame=%u bytes=%u\n",contentHits/30,contentStores/30,contentBytes);contentHits=contentStores=0;
 printf("[G3-STATS] frames=%u lists/frame=%u bytes/frame=%u same=%u changed=%u new=%u\n",g3StatFrames,g3StatLists/30,g3StatBytes/30,g3StatSame/30,g3StatChanged/30,g3StatNew/30);
 printf("[G3-STATS] us/frame: hit=%u(%u) record=%u(%u) plain=%u(%u) imm=%u(%u) draw=%u(%u)\n",g3CatUs[0]/30,g3CatN[0]/30,g3CatUs[1]/30,g3CatN[1]/30,g3CatUs[2]/30,g3CatN[2]/30,g3CatUs[3]/30,g3CatN[3]/30,g3DrawUs/30,g3DrawN/30);
 printf("[G3-STATS] hit sub-steps us/frame: compare=%u state=%u draw=%u restore=%u | misses/frame bytes=%u state=%u nosrc=%u\n",g3HitUs[0]/30,g3HitUs[1]/30,g3HitUs[2]/30,g3HitUs[3]/30,listMissBytes/30,listMissState/30,listMissNoSrc/30);memset(g3HitUs,0,sizeof g3HitUs);listMissBytes=listMissState=listMissNoSrc=0;
 {char line[600];int n=snprintf(line,sizeof line,"[G3-STATS] generic-op us/frame:");for(int i=0;i<64;i++)if(g3OpN[i])n+=snprintf(line+n,sizeof line-n," %s=%u(%u)",g3OpName[i<48?i:47],g3OpUs[i]/30,g3OpN[i]/30);printf("%s\n",line);}
 memset(g3OpUs,0,sizeof g3OpUs);memset(g3OpN,0,sizeof g3OpN);
 memset(g3CatUs,0,sizeof g3CatUs);memset(g3CatN,0,sizeof g3CatN);g3DrawUs=g3DrawN=0;
 char line[512];int n=0;n+=snprintf(line+n,sizeof line-n,"[G3-STATS] list:");for(int i=0;i<64;i++)if(g3StatList[i])n+=snprintf(line+n,sizeof line-n," %d=%u",i,g3StatList[i]/30);printf("%s\n",line);
 n=0;n+=snprintf(line+n,sizeof line-n,"[G3-STATS] imm:");for(int i=0;i<64;i++)if(g3StatImm[i])n+=snprintf(line+n,sizeof line-n," %d=%u",i,g3StatImm[i]/30);printf("%s\n",line);
 memset(g3StatList,0,sizeof g3StatList);memset(g3StatImm,0,sizeof g3StatImm);g3StatLists=g3StatBytes=g3StatSame=g3StatChanged=g3StatNew=0;
}
#define G3STAT_LIST(t) (g3StatList[(t)&63]++)
#define G3STAT_IMM(t) (g3StatImm[(t)&63]++)
#define G3CAT_BEGIN() unsigned catT0=sceKernelGetSystemTimeLow()
#define G3CAT_END(c) (g3CatUs[c]+=sceKernelGetSystemTimeLow()-catT0,g3CatN[c]++)
#else
#define G3CAT_BEGIN() ((void)0)
#define G3CAT_END(c) ((void)0)
#define G3STAT_LIST(t) ((void)0)
#define G3STAT_IMM(t) ((void)0)
static inline void G3StatsList(const void*,unsigned){}
static inline void G3StatsSwap(){}
#endif
static void SIM_HandleG3CommandImpl(draw_msg_t *msg);
static void ProcessCommandList(draw_msg_t *msg);
static unsigned profileDepth;
static unsigned profOuterN;
/* Timer syscalls are not free on hardware: sample one outer call in eight and scale. */
void SIM_HandleG3Command(draw_msg_t *msg){bool outer=profileDepth++==0;if(outer)G3STAT_IMM(msg->type);bool sample=outer&&((++profOuterN&7)==0);unsigned t=sample?sceKernelGetSystemTimeLow():0;SIM_HandleG3CommandImpl(msg);--profileDepth;if(sample){unsigned dt=(sceKernelGetSystemTimeLow()-t)*8;g3ProfUs+=dt;
#ifdef VITAPOKE_G3_STATS
 if(msg->type!=DRAW_CMD_G3_CMD_LIST){g3CatUs[3]+=dt;g3CatN[3]++;}
#endif
}g3ProfCalls++;}
static void SIM_HandleG3CommandImpl(draw_msg_t *msg) {
  u32 temp;
  switch (msg->type) {
  case DRAW_CMD_G3_NOP:
  case DRAW_CMD_G3_DUMMY:
    break;
  case DRAW_CMD_G3_BEGIN:
    G3SIM_Begin(static_cast<GXBegin>(msg->data.numU8));
    break;
  case DRAW_CMD_G3_MTXMODE:
    G3SIM_MtxMode(static_cast<GXMtxMode>(msg->data.numU8));
    break;
  case DRAW_CMD_G3_IDENTITY:
    G3SIM_Identity();
    break;
  case DRAW_CMD_G3_PUSHMTX:
    G3SIM_MtxPush();
    break;
  case DRAW_CMD_G3_POPMTX:
    G3SIM_MtxPop(msg->data.numU8);
    break;
  case DRAW_CMD_G3_STOREMTX:
    G3SIM_MtxStore(msg->data.numU8);
    break;
  case DRAW_CMD_G3_RESTOREMTX:
    G3SIM_MtxRestore(msg->data.numU8);
    break;
  case DRAW_CMD_G3_LOADMTX44:
    G3SIM_MtxLoad44(&msg->data.mtx44);
    break;
  case DRAW_CMD_G3_LOADMTX43:
    G3SIM_MtxLoad43(&msg->data.mtx43);
    break;
  case DRAW_CMD_G3_MTXMULT33:
    G3SIM_MtxMult33(&msg->data.mtx33);
    break;
  case DRAW_CMD_G3_MTXMULT43:
    G3SIM_MtxMult43(&msg->data.mtx43);
    break;
  case DRAW_CMD_G3_MTXMULT44:
    G3SIM_MtxMult44(&msg->data.mtx44);
    break;
  case DRAW_CMD_G3_SCALE:
    G3SIM_MtxScale(msg->data.scale);
    break;
  case DRAW_CMD_G3_TRANSLATE:
    G3SIM_MtxTranslate(msg->data.scale);
    break;
  case DRAW_CMD_G3_COLOR:
    G3SIM_Color(msg->data.numU16);
    break;
  case DRAW_CMD_G3_TEXIMAGEPARAM:

    G3SIM_FlushArray();
    G3SIM_TexImageParam(msg->data.numU32);
    break;
  case DRAW_CMD_G3_VTXXY:
    msg->type = DRAW_CMD_G3_VTX16;
    msg->data.xyz.z = prevXYZ.z;
    SIM_HandleG3Command(msg);
    break;
  case DRAW_CMD_G3_VTXXZ:
    msg->type = DRAW_CMD_G3_VTX16;
    msg->data.xyz.y = prevXYZ.y;
    SIM_HandleG3Command(msg);
    break;
  case DRAW_CMD_G3_VTXYZ:
    msg->type = DRAW_CMD_G3_VTX16;
    msg->data.xyz.x = prevXYZ.x;
    SIM_HandleG3Command(msg);
    break;
  case DRAW_CMD_G3_VTXDIFF: {
    // The conversion works a bit different from vtx10
    msg->type = DRAW_CMD_G3_VTX16;
    temp = msg->data.numU32;

    s16 x = (s16)((temp & 0x000003FF) << 6) >> 6;
    s16 y = (s16)((temp & 0x000FFC00) >> 4) >> 6;
    s16 z = (s16)((temp & 0x3FF00000) >> 14) >> 6;

    msg->data.xyz.x = prevXYZ.x + x;
    msg->data.xyz.y = prevXYZ.y + y;
    msg->data.xyz.z = prevXYZ.z + z;
    SIM_HandleG3Command(msg);
  } break;
  case DRAW_CMD_G3_VTX10:
    msg->type = DRAW_CMD_G3_VTX16;
    // Convert to fx16
    temp = msg->data.numU32;
    msg->data.xyz.x = (temp & 0b1111111111) << 6;
    msg->data.xyz.y = ((temp >> 10) & 0b1111111111) << 6;
    msg->data.xyz.z = ((temp >> 20) & 0b1111111111) << 6;
    SIM_HandleG3Command(msg);
    break;
  case DRAW_CMD_G3_VTX16:
    prevXYZ.x = msg->data.xyz.x;
    prevXYZ.y = msg->data.xyz.y;
    prevXYZ.z = msg->data.xyz.z;
    G3SIM_VtxAssemble(msg->data.xyz.x,msg->data.xyz.y,msg->data.xyz.z);
    break;
  case DRAW_CMD_G3_LIGHTCOLOR:
    G3SIM_LightColor(msg->data.numU32);
    break;
  case DRAW_CMD_G3_LIGHTVECTOR:
    G3SIM_LightVector(msg->data.numU32);
    break;
  case DRAW_CMD_G3_NORMAL:
    G3SIM_Normal(msg->data.numU32);
    break;
  case DRAW_CMD_G3_TEXCOORD:
    G3SIM_TexCoord(msg->data.numU32);
    break;
  case DRAW_CMD_G3_POLYGONATTR:
    G3SIM_PolygonAttr(msg->data.numU32);
    break;
  case DRAW_CMD_G3_DIFF_AMB:
    G3SIM_DiffAmb(msg->data.numU32);
    break;
  case DRAW_CMD_G3_SPEC_EMI:
    G3SIM_SpecEmi(msg->data.numU32);
    break;
  case DRAW_CMD_G3_DIRECT0:
    msg->type = DecodeG3Op(msg->data.numU32);
    SIM_HandleG3Command(msg);
    break;
  case DRAW_CMD_G3_DIRECT1:
    msg->type = DecodeG3Op(msg->data.numsU64[0]);
    // Get the parameter in the right place
    ConvertMsgParams(msg);
    SIM_HandleG3Command(msg);
    break;
  case DRAW_CMD_G3_SWAPBUFFERS: {
    G3StatsSwap();listFrame++;
#ifdef VITAPOKE_LIST_TRACE
    printf("[LT-FRAME] listFrame=%u\n",listFrame);
#endif
    u8 depthBufferMode = (msg->data.numU32 & 2) >> 1;
    G3SIM_SwapBuffers(
        depthBufferMode); // TODO: add flush attributes here for Z/W-buffering!
  } break;
  case DRAW_CMD_G3_VIEWPORT:
    // TODO
    break;
  case DRAW_CMD_G3_TEXPLTTBASE:
    G3SIM_FlushArray();
    G3SIM_TexPlttBase(msg->data.numU32);
    break;
  case DRAW_CMD_G3_BOXTEST:
    G3SIM_BoxTest(msg->data.boxtest.x, msg->data.boxtest.y, msg->data.boxtest.z,
                  msg->data.boxtest.width, msg->data.boxtest.height,
                  msg->data.boxtest.depth);
    break;
  case DRAW_CMD_G3_CMD_LIST:
    ProcessCommandList(msg);
    /* The command list is the caller's, and it is not freed here.
     *
     * This free is inherited from a design where the list was posted to a drawing thread, which
     * meant the poster had to hand over a copy. Nothing in this port does that: MI_SendGXCommand
     * runs the simulator synchronously on the caller's own buffer, which is the DS's contract for
     * it, and that buffer is in the game's arena inside s_HW_MAIN_MEM. Handing one of those to
     * libc's free sends the allocator walking a chunk header made of whatever DS data sits in front
     * of it -- which is how this was found: the game ran for exactly thirty seconds and then died
     * inside _free_r with a pointer into the DS's own memory.
     */
    break;
  default:
    break;
    // printf( "Draw command %d not implemented\n", msg->type );
  }
}

extern "C" unsigned ModelFrontendPolygons(){return s_numG3DrawsThisFrame;}

static const unsigned long long kPureOps=(1ULL<<DRAW_CMD_G3_NOP)|(1ULL<<DRAW_CMD_G3_COLOR)|(1ULL<<DRAW_CMD_G3_NORMAL)|(1ULL<<DRAW_CMD_G3_TEXCOORD)|(1ULL<<DRAW_CMD_G3_VTX)|(1ULL<<DRAW_CMD_G3_VTX10)|(1ULL<<DRAW_CMD_G3_VTX16)|(1ULL<<DRAW_CMD_G3_VTXXY)|(1ULL<<DRAW_CMD_G3_VTXXZ)|(1ULL<<DRAW_CMD_G3_VTXYZ)|(1ULL<<DRAW_CMD_G3_VTXDIFF)|(1ULL<<DRAW_CMD_G3_BEGIN)|(1ULL<<DRAW_CMD_G3_END);
/* ---- decode optimisations (opt-decode experiment) ----
   OPT_PRESCAN: decide list purity from the op bytes before recording, so impure (setup) lists never
   pay the content hash/probe, entry-state capture, record begin/end and impure-slot marking.
   OPT_FASTDECODE: copy the payload of frequent setup ops straight into the message fields the
   handler reads, instead of widening every parameter to 64 bits and running ConvertMsgParams.
   VITAPOKE_DECODE_ORACLE: test build; every fast-decoded op is also run through the generic
   decoder on a copy of the same message and the handler-visible bytes and advance are compared,
   and the prescan result is compared with the loop's own purity result. */
#if defined(OPT_PRESCAN)||defined(OPT_FASTDECODE)||defined(VITAPOKE_DECODE_ORACLE)
struct G3OpInfo{u8 type,size,pure;};
static G3OpInfo g3OpInfo[256];static bool g3OpInfoReady;
static void G3OpInfoBuild(){
 for(unsigned op=0;op<256;op++){draw_command_type_t t=DecodeG3Op(op);unsigned sz;
  switch(t){
  case DRAW_CMD_G3_NORMAL:case DRAW_CMD_G3_COLOR:case DRAW_CMD_G3_TEXCOORD:case DRAW_CMD_G3_VTX10:case DRAW_CMD_G3_VTXDIFF:case DRAW_CMD_G3_POLYGONATTR:
  case DRAW_CMD_G3_TEXIMAGEPARAM:case DRAW_CMD_G3_TEXPLTTBASE:case DRAW_CMD_G3_LIGHTVECTOR:case DRAW_CMD_G3_LIGHTCOLOR:case DRAW_CMD_G3_DIFF_AMB:case DRAW_CMD_G3_SPEC_EMI:
  case DRAW_CMD_G3_VTXXY:case DRAW_CMD_G3_VTXXZ:case DRAW_CMD_G3_VTXYZ:sz=4;break;
  case DRAW_CMD_G3_VTX16:case DRAW_CMD_G3_VTX:sz=8;break;
  case DRAW_CMD_G3_NOP:sz=0;break;
  default:sz=GetNumParamsFromCommandType(t)*GetParamSizeFromCommandType(t);break;}
  g3OpInfo[op].type=(u8)t;g3OpInfo[op].size=(u8)sz;g3OpInfo[op].pure=(t<64&&((kPureOps>>t)&1))?1:0;}
 g3OpInfoReady=true;}
/* Mirrors the decode loop's block walk exactly (4 ops per block, then their payloads). */
static bool ListAllPure(const void*p,unsigned size){const u8*b=(const u8*)p;unsigned off=0;
 while(off<size){const u8*ops=b+off;off+=4;for(int i=0;i<4;i++){const G3OpInfo&oi=g3OpInfo[ops[i]];if(!oi.pure)return false;off+=oi.size;}}
 return true;}
#endif
static inline unsigned GenericDecode(draw_msg_t*m,void*paramPtr){
  u8 numParams=GetNumParamsFromCommandType(m->type),paramSize=GetParamSizeFromCommandType(m->type),paramSigned=isParamSigned(m->type);
  for(int j=1;j<numParams+1;j++){
    switch(paramSize){
    case 1:if(paramSigned)m->data.numsS64[j]=(s64)*(s8*)paramPtr;else m->data.numsU64[j]=*(u8*)paramPtr;break;
    case 2:if(paramSigned)m->data.numsS64[j]=(s64)*(s16*)paramPtr;else m->data.numsU64[j]=*(u16*)paramPtr;break;
    case 4:if(paramSigned)m->data.numsS64[j]=(s64)*(s32*)paramPtr;else m->data.numsU64[j]=*(u32*)paramPtr;break;
    case 8:if(paramSigned)m->data.numsS64[j]=*(s64*)paramPtr;else m->data.numsU64[j]=*(u64*)paramPtr;break;
    default:break;}
    paramPtr+=paramSize;}
  return (unsigned)numParams*paramSize;}
#ifdef VITAPOKE_DECODE_ORACLE
static unsigned oracleChecked,oracleMismatch,oraclePureChecked,oraclePureMismatch;
static void OracleOp(const draw_msg_t*before,const draw_msg_t*fast,void*paramPtr,unsigned fastSize,unsigned fieldBytes){
  draw_msg_t g;memcpy(&g,before,sizeof g);unsigned gsz=GenericDecode(&g,paramPtr);ConvertMsgParams(&g);
  unsigned n=fieldBytes<4?4:fieldBytes;oracleChecked++;
  if(gsz!=fastSize||g.type!=fast->type||memcmp(&g.data,&fast->data,n)){if(oracleMismatch++<20)printf("[DECODE-ORACLE] MISMATCH type=%d gsz=%u fsz=%u\n",(int)fast->type,gsz,fastSize);}
  if(!(oracleChecked%200000))printf("[DECODE-ORACLE] checked=%u mismatches=%u pure_checked=%u pure_mismatches=%u\n",oracleChecked,oracleMismatch,oraclePureChecked,oraclePureMismatch);}
#endif
static void ProcessCommandList(draw_msg_t *msg){
#if defined(OPT_PRESCAN)||defined(OPT_FASTDECODE)||defined(VITAPOKE_DECODE_ORACLE)
    if(!g3OpInfoReady)G3OpInfoBuild();
#endif
    const void*listPtr=msg->data.ptr;unsigned listSize=msg->size;G3CAT_BEGIN();
#ifdef VITAPOKE_LIST_TRACE
    bool lt=listFrame>=700&&listFrame<=900;unsigned ltPolys0=s_numG3DrawsThisFrame;extern unsigned g3RecCount;if(lt)g3RecCount=0;
#endif
    bool rec=false,pure=true,prePure=true;(void)prePure;ListEntry*le=nullptr;G3GeomEntryState entryState;unsigned contentH=0;bool contentTry=false;xyz_s_t prevIn=prevXYZ;unsigned polys0=s_numG3DrawsThisFrame,nonRaw0=g3NonRawSubmits;
#if VITAPOKE_LIST_CACHE
    le=ListFind(listPtr);
    if(le&&le->impure&&(le->impureSize!=listSize||le->impureSig!=ListSig(listPtr,listSize)))le->impure=0;
#ifdef VITAPOKE_G3_STATS
    if(le&&!le->src&&!le->impure)listMissNoSrc++;
#endif
#ifdef VITAPOKE_LIST_CACHE_VERIFY
    ListEntry*verify=nullptr;G3SlotState verifyEntrySlots;G3SIM_CaptureSlots(&verifyEntrySlots);
#endif
    if(le&&le->src){
#ifdef VITAPOKE_G3_STATS
      unsigned h0=sceKernelGetSystemTimeLow();bool bytesSame=le->size==listSize&&G3FastEqual(le->src,listPtr,listSize);unsigned h1=sceKernelGetSystemTimeLow();g3HitUs[0]+=h1-h0;
      bool stateSame=bytesSame&&(!(le->deps&G3DEP_PREV)||(prevXYZ.x==le->prevIn.x&&prevXYZ.y==le->prevIn.y&&prevXYZ.z==le->prevIn.z))&&G3SIM_EntryStateMatches(&le->entry,le->deps);unsigned h2=sceKernelGetSystemTimeLow();g3HitUs[1]+=h2-h1;
      if(!bytesSame)listMissBytes++;else if(!stateSame)listMissState++;
      if(stateSame){
#else
      if(le->size==listSize&&G3FastEqual(le->src,listPtr,listSize)&&(!(le->deps&G3DEP_PREV)||(prevXYZ.x==le->prevIn.x&&prevXYZ.y==le->prevIn.y&&prevXYZ.z==le->prevIn.z))&&G3SIM_EntryStateMatches(&le->entry,le->deps)){
#endif
#ifdef VITAPOKE_LIST_CACHE_VERIFY
        verify=le;goto record_anyway;   /* oracle: decode normally, then compare with the entry */
#endif
        if(le->nverts){G3SIM_ReplayRawBegin();G3ListDrawDirect(le->verts,le->nverts);}
#ifdef VITAPOKE_G3_STATS
        unsigned h3=sceKernelGetSystemTimeLow();g3HitUs[2]+=h3-h2;
#endif
        G3SIM_RestoreSlots(&le->exit);prevXYZ=le->prevOut;s_numG3DrawsThisFrame+=le->polys;
#ifdef VITAPOKE_G3_STATS
        g3HitUs[3]+=sceKernelGetSystemTimeLow()-h3;
#endif
        le->lastUse=listFrame;le->mismatches=0;listHits++;listHitVerts+=le->nverts;G3StatsList(listPtr,listSize);G3CAT_END(0);
#ifdef VITAPOKE_LIST_TRACE
        if(lt)printf("[LT] f=%u ptr=%p size=%u hit=1 n=%u polys=%u\n",listFrame,listPtr,listSize,le->nverts,le->polys);
#endif
        return;
      }
      /* Same address, different bytes or state: a dynamic buffer. Re-record a few times, then
         leave it alone for a while so a per-frame buffer does not churn the cache. */
      ListRelease(le);
      le->lastUse=listFrame;if(++le->mismatches>=4){le->mismatches=0;le->skipUntil=listFrame+300;listVolatile++;}
    }
#ifdef OPT_PRESCAN
    prePure=ListAllPure(listPtr,listSize);
#endif
    contentTry=prePure&&G3SIM_ReplayEligible()&&listSize<=kListMaxOne&&listSize>=8&&!(le&&le->impure);
    if(contentTry){contentH=ContentHash(listPtr,listSize);ListEntry*ce=ContentFind(listSize,contentH);
      if(ce&&G3FastEqual(ce->src,listPtr,listSize)&&(!(ce->deps&G3DEP_PREV)||(prevXYZ.x==ce->prevIn.x&&prevXYZ.y==ce->prevIn.y&&prevXYZ.z==ce->prevIn.z))&&G3SIM_EntryStateMatches(&ce->entry,ce->deps)){
#ifdef VITAPOKE_LIST_CACHE_VERIFY
        verify=ce;goto record_anyway;
#endif
        if(ce->nverts){G3SIM_ReplayRawBegin();G3ListDrawDirect(ce->verts,ce->nverts);}
        G3SIM_RestoreSlots(&ce->exit);prevXYZ=ce->prevOut;s_numG3DrawsThisFrame+=ce->polys;
        ce->lastUse=listFrame;listHits++;contentHits++;listHitVerts+=ce->nverts;G3StatsList(listPtr,listSize);G3CAT_END(0);
        return;
      }
    }
    rec=prePure&&G3SIM_ReplayEligible()&&listSize<=kListMaxOne&&!(le&&le->impure);
#ifdef VITAPOKE_LIST_CACHE_VERIFY
    record_anyway: if(verify)rec=true;
#endif
    if(rec){G3SIM_CaptureEntryState(&entryState,~0u);G3ListRecordBegin();}
#endif
    unsigned deps=0,seen=0;(void)deps;(void)seen;   /* seen: 1 texcoord, 2 colour/normal, 4 full vertex */
    // Process a command List
    {
      G3SIM_CommandBlock_t *cmdBlockPtr;
      draw_msg_t tempMsg;
      cmdBlockPtr = static_cast<G3SIM_CommandBlock_t *>(msg->data.ptr);
      G3StatsList(msg->data.ptr,msg->size);
      u64 curOffset;
      curOffset = 0;
      u8 numParams;
      void *paramPtr;
      u8 paramSize;
      u8 paramSigned;
      while (curOffset < msg->size) {
        paramPtr = (void *)cmdBlockPtr + 4;
        curOffset += 4;
        for (int i = 0; i < 4; i++) {
#ifdef OPT_OPTABLE
          {const G3OpInfo&oi=g3OpInfo[cmdBlockPtr->ops[i]];tempMsg.type=(draw_command_type_t)oi.type;G3STAT_LIST(tempMsg.type);
          if(!oi.pure)pure=false;}
          if(0);
#else
          tempMsg.type = DecodeG3Op(cmdBlockPtr->ops[i]);G3STAT_LIST(tempMsg.type);
          if(!((kPureOps>>(tempMsg.type&63))&1)||tempMsg.type>=64)pure=false;
#endif
          else switch(tempMsg.type){
           case DRAW_CMD_G3_TEXCOORD:seen|=1;deps|=G3DEP_TEXCOORD;break;
           case DRAW_CMD_G3_COLOR:seen|=2;break;
           case DRAW_CMD_G3_NORMAL:seen|=2;deps|=G3DEP_LIGHT;break;
           case DRAW_CMD_G3_BEGIN:seen|=8;break;
           case DRAW_CMD_G3_VTX:case DRAW_CMD_G3_VTX16:case DRAW_CMD_G3_VTX10:
            if(!(seen&1))deps|=G3DEP_TEX;if(!(seen&2))deps|=G3DEP_COLOR;if(!(seen&8))deps|=G3DEP_SLOTS;seen|=4;break;
           case DRAW_CMD_G3_VTXXY:case DRAW_CMD_G3_VTXXZ:case DRAW_CMD_G3_VTXYZ:case DRAW_CMD_G3_VTXDIFF:
            if(!(seen&1))deps|=G3DEP_TEX;if(!(seen&2))deps|=G3DEP_COLOR;if(!(seen&4))deps|=G3DEP_PREV;if(!(seen&8))deps|=G3DEP_SLOTS;seen|=4;break;
           default:break;}
          // Packed payloads already have the native scalar representation.
          // Execute the known operation directly; preserve flushes and vertex state.
          switch(tempMsg.type){
          case DRAW_CMD_G3_NORMAL:
tempMsg.data.numU32=*(u32*)paramPtr;G3SIM_Normal(tempMsg.data.numU32);paramPtr+=4;curOffset+=4;g3ProfCalls++;continue;
          case DRAW_CMD_G3_COLOR:
tempMsg.data.numU32=*(u32*)paramPtr;G3SIM_Color(tempMsg.data.numU32);paramPtr+=4;curOffset+=4;g3ProfCalls++;continue;
          case DRAW_CMD_G3_TEXCOORD:
tempMsg.data.numU32=*(u32*)paramPtr;G3SIM_TexCoord(tempMsg.data.numU32);paramPtr+=4;curOffset+=4;g3ProfCalls++;continue;
          case DRAW_CMD_G3_POLYGONATTR:
tempMsg.data.numU32=*(u32*)paramPtr;G3SIM_PolygonAttr(tempMsg.data.numU32);paramPtr+=4;curOffset+=4;g3ProfCalls++;continue;
          case DRAW_CMD_G3_TEXIMAGEPARAM:
            G3SIM_FlushArray();tempMsg.data.numU32=*(u32*)paramPtr;G3SIM_TexImageParam(tempMsg.data.numU32);paramPtr+=4;curOffset+=4;g3ProfCalls++;continue;
          case DRAW_CMD_G3_TEXPLTTBASE:
            G3SIM_FlushArray();tempMsg.data.numU32=*(u32*)paramPtr;G3SIM_TexPlttBase(tempMsg.data.numU32);paramPtr+=4;curOffset+=4;g3ProfCalls++;continue;
          case DRAW_CMD_G3_LIGHTVECTOR:
tempMsg.data.numU32=*(u32*)paramPtr;G3SIM_LightVector(tempMsg.data.numU32);paramPtr+=4;curOffset+=4;g3ProfCalls++;continue;
          case DRAW_CMD_G3_LIGHTCOLOR:
tempMsg.data.numU32=*(u32*)paramPtr;G3SIM_LightColor(tempMsg.data.numU32);paramPtr+=4;curOffset+=4;g3ProfCalls++;continue;
          case DRAW_CMD_G3_DIFF_AMB:
tempMsg.data.numU32=*(u32*)paramPtr;G3SIM_DiffAmb(tempMsg.data.numU32);paramPtr+=4;curOffset+=4;g3ProfCalls++;continue;
          case DRAW_CMD_G3_SPEC_EMI:
tempMsg.data.numU32=*(u32*)paramPtr;G3SIM_SpecEmi(tempMsg.data.numU32);paramPtr+=4;curOffset+=4;g3ProfCalls++;continue;
          case DRAW_CMD_G3_VTX16:case DRAW_CMD_G3_VTX:
            prevXYZ.x=((s16*)paramPtr)[0];prevXYZ.y=((s16*)paramPtr)[1];prevXYZ.z=((s16*)paramPtr)[2];paramPtr+=8;curOffset+=8;
            tempMsg.data.xyz=prevXYZ;G3SIM_VtxAssemble(prevXYZ.x,prevXYZ.y,prevXYZ.z);g3ProfCalls++;continue;
          case DRAW_CMD_G3_VTXXY:
            prevXYZ.x=((s16*)paramPtr)[0];prevXYZ.y=((s16*)paramPtr)[1];paramPtr+=4;curOffset+=4;
            tempMsg.data.xyz=prevXYZ;G3SIM_VtxAssemble(prevXYZ.x,prevXYZ.y,prevXYZ.z);g3ProfCalls+=2;continue;
          case DRAW_CMD_G3_VTXXZ:
            prevXYZ.x=((s16*)paramPtr)[0];prevXYZ.z=((s16*)paramPtr)[1];paramPtr+=4;curOffset+=4;
            tempMsg.data.xyz=prevXYZ;G3SIM_VtxAssemble(prevXYZ.x,prevXYZ.y,prevXYZ.z);g3ProfCalls+=2;continue;
          case DRAW_CMD_G3_VTXYZ:
            prevXYZ.y=((s16*)paramPtr)[0];prevXYZ.z=((s16*)paramPtr)[1];paramPtr+=4;curOffset+=4;
            tempMsg.data.xyz=prevXYZ;G3SIM_VtxAssemble(prevXYZ.x,prevXYZ.y,prevXYZ.z);g3ProfCalls+=2;continue;
          case DRAW_CMD_G3_VTX10:{
            u32 v=*(u32*)paramPtr;prevXYZ.x=(v&1023)<<6;prevXYZ.y=((v>>10)&1023)<<6;prevXYZ.z=((v>>20)&1023)<<6;paramPtr+=4;curOffset+=4;
            tempMsg.data.xyz=prevXYZ;G3SIM_VtxAssemble(prevXYZ.x,prevXYZ.y,prevXYZ.z);g3ProfCalls+=2;continue;}
          case DRAW_CMD_G3_VTXDIFF:{
            u32 v=*(u32*)paramPtr;s16 x=(s16)((v&1023)<<6)>>6,y=(s16)((v&0xffc00)>>4)>>6,z=(s16)((v&0x3ff00000)>>14)>>6;
            prevXYZ.x+=x;prevXYZ.y+=y;prevXYZ.z+=z;paramPtr+=4;curOffset+=4;
            tempMsg.data.xyz=prevXYZ;G3SIM_VtxAssemble(prevXYZ.x,prevXYZ.y,prevXYZ.z);g3ProfCalls+=2;continue;}
          case DRAW_CMD_G3_NOP:continue;
#ifdef OPT_FASTDECODE
#ifdef VITAPOKE_DECODE_ORACLE
#define ORACLE_PRE draw_msg_t oracleBefore;memcpy(&oracleBefore,&tempMsg,sizeof tempMsg);
#define ORACLE_POST(sz,fb) OracleOp(&oracleBefore,&tempMsg,paramPtr,sz,fb);
#else
#define ORACLE_PRE
#define ORACLE_POST(sz,fb)
#endif
          case DRAW_CMD_G3_MTXMODE:case DRAW_CMD_G3_STOREMTX:case DRAW_CMD_G3_RESTOREMTX:case DRAW_CMD_G3_BEGIN:
            {ORACLE_PRE tempMsg.data.numU8=*(u8*)paramPtr;ORACLE_POST(4,1)}paramPtr+=4;curOffset+=4;
            SIM_HandleG3CommandImpl(&tempMsg);g3ProfCalls++;continue;
          case DRAW_CMD_G3_POPMTX:
            {ORACLE_PRE tempMsg.data.numU8=*(u8*)paramPtr;ORACLE_POST(4,1)}paramPtr+=4;curOffset+=4;
            SIM_HandleG3CommandImpl(&tempMsg);g3ProfCalls++;continue;
          case DRAW_CMD_G3_PUSHMTX:case DRAW_CMD_G3_IDENTITY:case DRAW_CMD_G3_END:
            {ORACLE_PRE ORACLE_POST(0,0)}
            SIM_HandleG3CommandImpl(&tempMsg);g3ProfCalls++;continue;
          case DRAW_CMD_G3_VIEWPORT:   /* handler ignores the payload */
            {ORACLE_PRE ORACLE_POST(4,0)}paramPtr+=4;curOffset+=4;
            g3ProfCalls++;continue;
          case DRAW_CMD_G3_SCALE:case DRAW_CMD_G3_TRANSLATE:
            {ORACLE_PRE memcpy(tempMsg.data.scale,paramPtr,12);ORACLE_POST(12,12)}paramPtr+=12;curOffset+=12;
            SIM_HandleG3CommandImpl(&tempMsg);g3ProfCalls++;continue;
          case DRAW_CMD_G3_MTXMULT33:
            {ORACLE_PRE memcpy(&tempMsg.data.mtx33,paramPtr,36);ORACLE_POST(36,36)}paramPtr+=36;curOffset+=36;
            SIM_HandleG3CommandImpl(&tempMsg);g3ProfCalls++;continue;
          case DRAW_CMD_G3_LOADMTX43:case DRAW_CMD_G3_MTXMULT43:
            {ORACLE_PRE memcpy(&tempMsg.data.mtx43,paramPtr,48);ORACLE_POST(48,48)}paramPtr+=48;curOffset+=48;
            SIM_HandleG3CommandImpl(&tempMsg);g3ProfCalls++;continue;
          case DRAW_CMD_G3_LOADMTX44:case DRAW_CMD_G3_MTXMULT44:
            {ORACLE_PRE memcpy(&tempMsg.data.mtx44,paramPtr,64);ORACLE_POST(64,64)}paramPtr+=64;curOffset+=64;
            SIM_HandleG3CommandImpl(&tempMsg);g3ProfCalls++;continue;
          case DRAW_CMD_G3_BOXTEST:
            {ORACLE_PRE memcpy(&tempMsg.data.boxtest,paramPtr,12);ORACLE_POST(12,12)}paramPtr+=12;curOffset+=12;
            SIM_HandleG3CommandImpl(&tempMsg);g3ProfCalls++;continue;
#endif
          default:break;
          }
          numParams = GetNumParamsFromCommandType(tempMsg.type);
          paramSize = GetParamSizeFromCommandType(tempMsg.type);
          paramSigned = isParamSigned(tempMsg.type);
          for (int j = 1; j < numParams + 1; j++) {
            switch (paramSize) {
            case 0:
              break;
            case 1:
              if (paramSigned) {
                tempMsg.data.numsS64[j] = (s64) * (s8 *)paramPtr;
              } else {
                tempMsg.data.numsU64[j] = *(u8 *)paramPtr;
              }
              break;
            case 2:
              if (paramSigned) {
                tempMsg.data.numsS64[j] = (s64) * (s16 *)paramPtr;
              } else {
                tempMsg.data.numsU64[j] = *(u16 *)paramPtr;
              }
              break;
            case 4:
              if (paramSigned) {
                tempMsg.data.numsS64[j] = (s64) * (s32 *)paramPtr;
              } else {
                tempMsg.data.numsU64[j] = *(u32 *)paramPtr;
              }
              break;
            case 8:
              if (paramSigned) {
                tempMsg.data.numsS64[j] = *(s64 *)paramPtr;
              } else {
                tempMsg.data.numsU64[j] = *(u64 *)paramPtr;
              }
              break;
            default:
              break;
            }
            paramPtr += paramSize;
            curOffset += paramSize;
          }

          ConvertMsgParams(&tempMsg);
#if defined(VITAPOKE_G3_STATS)&&!defined(VITAPOKE_G3_STATS_NOOPTIME)
          {unsigned ot=sceKernelGetSystemTimeLow();SIM_HandleG3Command(&tempMsg);g3OpUs[tempMsg.type&63]+=sceKernelGetSystemTimeLow()-ot;g3OpN[tempMsg.type&63]++;}
#else
          SIM_HandleG3Command(&tempMsg);
#endif
        }
        cmdBlockPtr = static_cast<G3SIM_CommandBlock_t *>(paramPtr);
      }
    }
#ifdef VITAPOKE_DECODE_ORACLE
    oraclePureChecked++;if(ListAllPure(listPtr,listSize)!=pure){if(oraclePureMismatch++<20)printf("[DECODE-ORACLE] PURE MISMATCH size=%u\n",listSize);}
#endif
#ifdef VITAPOKE_LIST_TRACE
    if(lt)printf("[LT] f=%u ptr=%p size=%u hit=0 n=%u polys=%u rec=%d pure=%d deps=%u seen=%u impure=%d skip=%d\n",listFrame,listPtr,listSize,g3RecCount,s_numG3DrawsThisFrame-ltPolys0,(int)rec,(int)pure,deps,seen,le?(int)le->impure:-1,le?(int)(le->skipUntil>listFrame):-1);
#endif
    G3CAT_END(rec?1:2);
#if VITAPOKE_LIST_CACHE
    if(rec){
      unsigned n=0;const GuVertex*v=nullptr;int intact=G3ListRecordEnd(&n,&v);
      if(!intact)listBroken++;
      /* Cacheable: pure ops, every polygon on the GE path, no mid-list flush, at least one
         vertex, and the first vertex after a BEGIN in this list (a list continuing a primitive
         from a previous list depends on the entry slots and is left to the decoder). */
      bool ok=pure&&g3NonRawSubmits==nonRaw0&&intact&&(seen&4)&&!(deps&G3DEP_SLOTS);
      bool uncacheable=!pure||!(seen&4)||(deps&G3DEP_SLOTS);
#ifdef VITAPOKE_LIST_CACHE_VERIFY
      if(verify){
        static unsigned reported;G3SlotState ex;G3SIM_CaptureSlots(&ex);unsigned polys=s_numG3DrawsThisFrame-polys0;
        /* generations are relative; oob/zf are only meaningful once finished/zValid; slots the
           list did not write hold whatever was there before and are never read before written */
        auto canon=[](G3SlotState c){for(int i=0;i<4;i++){unsigned g=c.der[i].gen;c.der[i].gen=g==0?0:(g==c.gen?1:2);if(!c.der[i].zValid)c.der[i].zf=0;if(!c.der[i].finished)c.der[i].oob=0;}c.gen=0;return c;};
        G3SlotState ca=canon(ex),cb=canon(verify->exit),ce=canon(verifyEntrySlots);
        for(int i=0;i<4;i++)if(memcmp(&ca.verts[i],&ce.verts[i],sizeof ca.verts[i])==0&&memcmp(&ca.der[i],&ce.der[i],sizeof ca.der[i])==0){memset(&ca.verts[i],0,sizeof ca.verts[i]);memset(&cb.verts[i],0,sizeof cb.verts[i]);memset(&ca.der[i],0,sizeof ca.der[i]);memset(&cb.der[i],0,sizeof cb.der[i]);}
        bool same=ok&&n==verify->nverts&&(n==0||memcmp(v,verify->verts,n*sizeof(GuVertex))==0)&&polys==verify->polys&&memcmp(&ca,&cb,sizeof ca)==0&&prevXYZ.x==verify->prevOut.x&&prevXYZ.y==verify->prevOut.y&&prevXYZ.z==verify->prevOut.z;
        if(!same&&reported<40){reported++;const u8*b=(const u8*)listPtr;printf("[LIST-VERIFY] MISMATCH frame=%u ptr=%p size=%u pure=%d nonraw=%u n=%u cached_n=%u polys=%u cached_polys=%u exit_same=%d prev_same=%d ops=%02x%02x%02x%02x verts_same=%d\n",listFrame,listPtr,listSize,(int)pure,g3NonRawSubmits-nonRaw0,n,verify->nverts,polys,verify->polys,(int)(memcmp(&ex,&verify->exit,sizeof ex)==0),(int)(prevXYZ.x==verify->prevOut.x&&prevXYZ.y==verify->prevOut.y&&prevXYZ.z==verify->prevOut.z),b[0],b[1],b[2],b[3],(int)(n==verify->nverts&&(n==0||memcmp(v,verify->verts,n*sizeof(GuVertex))==0)));}
        static unsigned checked;if(++checked%10000==0)printf("[LIST-VERIFY] checked=%u\n",checked);
        return;
      }
#endif
      if(!ok){if(uncacheable){ListEntry*e=ListSlotFor(listPtr);if(e){e->impure=1;e->impureSize=listSize;e->impureSig=ListSig(listPtr,listSize);e->lastUse=listFrame;}}return;}
      bool toContent=le&&(le->skipUntil>listFrame||le->mismatches>0);
      ListEntry*e;
      if(toContent){ContentMakeRoom(listSize+n*sizeof(GuVertex));if(!contentH)contentH=ContentHash(listPtr,listSize);e=ContentSlotFor(contentH);}
      else{if(le&&le->skipUntil>listFrame)return;ListMakeRoom(listSize+n*sizeof(GuVertex));e=ListSlotFor(listPtr);}
      if(!e)return;
      e->src=(u8*)malloc(listSize);e->verts=n?(GuVertex*)memalign(16,n*sizeof(GuVertex)):nullptr;
      if(!e->src||(n&&!e->verts)){free(e->src);free(e->verts);e->src=nullptr;e->verts=nullptr;return;}
      memcpy(e->src,listPtr,listSize);if(n){memcpy(e->verts,v,n*sizeof(GuVertex));sceKernelDcacheWritebackRange(e->verts,n*sizeof(GuVertex));}
      /* keep only the entry-state fields this list depends on (masked copy compares equal to a masked capture) */
      {G3GeomEntryState m;memset(&m,0,sizeof m);m.alpha=entryState.alpha;m.sSize=entryState.sSize;m.tSize=entryState.tSize;
       if(deps&G3DEP_COLOR)memcpy(m.color,entryState.color,3);
       if(deps&G3DEP_LIGHT){m.lightFlag=entryState.lightFlag;memcpy(m.lightColor,entryState.lightColor,sizeof m.lightColor);memcpy(m.lightVector,entryState.lightVector,sizeof m.lightVector);memcpy(m.diffuse,entryState.diffuse,3);memcpy(m.ambient,entryState.ambient,3);memcpy(m.specular,entryState.specular,3);memcpy(m.emission,entryState.emission,3);memcpy(m.vec,entryState.vec,sizeof m.vec);}
       if(deps&G3DEP_TEX){m.texS=entryState.texS;m.texT=entryState.texT;}
       if(deps&G3DEP_TEXCOORD){m.transform=entryState.transform;if(m.transform==1)memcpy(m.texMtx,entryState.texMtx,sizeof m.texMtx);}
       entryState=m;}
      e->size=listSize;e->nverts=n;e->polys=s_numG3DrawsThisFrame-polys0;e->entry=entryState;e->deps=deps;e->prevIn=prevIn;e->prevOut=prevXYZ;G3SIM_CaptureSlots(&e->exit);
      e->lastUse=listFrame;e->impure=0;listRecords++;
      if(toContent){e->ptr=nullptr;e->impureSig=contentH;contentBytes+=listSize+n*sizeof(GuVertex);contentStores++;}
      else{listEntries++;listBytes+=listSize+n*sizeof(GuVertex);if(listBytes>listHighBytes)listHighBytes=listBytes;}
    }
#endif
}
extern "C" void VitaNativeG3CommandsBorrowed(const void*source,unsigned bytes){
 draw_msg_t msg;msg.type=DRAW_CMD_G3_CMD_LIST;msg.size=bytes;msg.data.ptr=const_cast<void*>(source);
 bool outer=profileDepth++==0;bool sample=outer&&((++profOuterN&7)==0);unsigned t=sample?sceKernelGetSystemTimeLow():0;
 ProcessCommandList(&msg);--profileDepth;
 if(sample)g3ProfUs+=(sceKernelGetSystemTimeLow()-t)*8;g3ProfCalls++;
}
