#include <nitro.h>
#include <nnsys.h>
#pragma pack(push,4)
#include "field/area_data.h"
#include "heap.h"
#include <stddef.h>
#include <stdio.h>
typedef struct SSAreaHeader {u16 props,textures,animated;u8 resourceKind,lighting;} SSAreaHeader;
typedef struct SSAreaLoad {u32 bank,zero;NARC *models;u16 count,pad;} SSAreaLoad;
struct AreaDataManager {
 u32 reserved;
 NNSG3dResFileHeader *modelFiles[550];
 void *mapTextureFile,*propTextureFile;
 NNSG3dResTex *mapTexture,*propTexture;
 AreaDataManager_Sub8AC *materialShape;
 SSAreaHeader area;
 SSAreaLoad *load;
 u16 *modelIDs;
 NARC *animations;
};
_Static_assert(sizeof(struct AreaDataManager)==0x8c4,"SS area data size");
_Static_assert(offsetof(struct AreaDataManager,area)==0x8b0,"SS area data header");
_Static_assert(offsetof(struct AreaDataManager,load)==0x8b8,"SS area load pointer");
_Static_assert(offsetof(struct AreaDataManager,animations)==0x8c0,"SS area animation archive");
AreaDataManager *AreaDataManager_Alloc(u8 bank){
 AreaDataManager *a=Heap_Alloc(HEAP_ID_FIELD1,sizeof(*a));
 a->load=Heap_AllocAtEnd(HEAP_ID_FIELD1,sizeof(*a->load));
 a->load->bank=bank;a->load->zero=0;
 ReadWholeNarcMemberByIdPair(&a->area,42,bank);
 a->animations=NARC_New(a->area.resourceKind?107:108,HEAP_ID_FIELD1);
 a->load->models=NARC_New(a->area.resourceKind?40:148,HEAP_ID_FIELD1);
 printf("[SS-AREA] bank=%u props=%u textures=%u animated=%u kind=%u lighting=%u\n",bank,a->area.props,a->area.textures,a->area.animated,a->area.resourceKind,a->area.lighting);
 return a;
}
NARC *ov01_021FB904(AreaDataManager *a){return a->animations;}
NNSG3dResFileHeader **AreaDataManager_GetMapPropModelFile(AreaDataManager *a){if(!a)GF_AssertFail();return a->modelFiles;}
NNSG3dResTex *AreaDataManager_GetMapTexture(AreaDataManager *a){if(!a)GF_AssertFail();return a->mapTexture;}
NNSG3dResTex *ov01_021FB9E0(AreaDataManager *a){if(!a)GF_AssertFail();return a->propTexture;}
AreaDataManager_Sub8AC *ov01_021FB9F4(AreaDataManager *a){return a->materialShape;}
BOOL ov01_021FBA00(AreaDataManager *a){return a->area.lighting!=0;}
u32 AreaDataManager_GetAreaLightArchiveID(AreaDataManager *a){return a->area.lighting==0?1:a->area.lighting==2?3:0;}
NNSG3dResFileHeader **ov01_021FB90C(u32 id,AreaDataManager *a){if(!a)GF_AssertFail();if(!a->modelFiles[id]){if(!a->modelFiles[0])GF_AssertFail();return a->modelFiles;}return &a->modelFiles[id];}

#include "gf_3d_render.h"
#include "field/overlay_01_02204004.h"
#include "overlay_01_021EA6C4.h"
#include <string.h>
extern const GXRgb ssdata_overlay_01_021FB878_ov01_02208BA0[8],ssdata_overlay_01_021FB878_ov01_02208BB0[8];
extern const char ssdata_overlay_01_021FB878_ov01_02209A88[],ssdata_overlay_01_021FB878_ov01_02209AB4[];
_Static_assert(sizeof(MapPropAnimListFile)==24,"SS model animation descriptor");
static void StripTexture(void *file,NNSG3dResTex *tex){Heap_Realloc(file,(u32)((u8 *)tex+tex->texInfo.ofsTex-(u8 *)file));}
static NNSG3dResMdl *FirstModel(NNSG3dResFileHeader *file){NNSG3dResMdlSet *set=NNS_G3dGetMdlSet(file);return set&&set->dict.numEntry?NNS_G3dGetMdlByIdx(set,0):NULL;}
void AreaDataManager_Load(AreaDataManager *a,void *unkC0,MapPropAnimationManager *anim,void *unkCC,void *unk104){
 SSAreaLoad *load=a->load;
 a->modelIDs=AllocAndReadWholeNarcMemberByIdPair(43,a->area.props,HEAP_ID_FIELD1);
 load->count=a->modelIDs[0];if(load->count>=550)GF_AssertFail();
 a->mapTextureFile=AllocAndReadWholeNarcMemberByIdPair(44,a->area.textures,HEAP_ID_FIELD1);
 a->propTextureFile=AllocAndReadWholeNarcMemberByIdPair(70,a->area.props,HEAP_ID_FIELD1);
 a->mapTexture=NNS_G3dGetTex(a->mapTextureFile);
 a->propTexture=load->count?NNS_G3dGetTex(a->propTextureFile):NULL;
 if(!GF3dRender_AllocAndLoadTexResources(a->mapTexture))GF_AssertFail();
 StripTexture(a->mapTextureFile,a->mapTexture);
 if(a->propTexture){if(!GF3dRender_AllocAndLoadTexResources(a->propTexture))GF_AssertFail();StripTexture(a->propTextureFile,a->propTexture);}
 if(a->area.animated!=0xffff)ov01_0220463C(unkCC,a->area.animated);
 for(int i=0;i<550;i++)a->modelFiles[i]=NULL;
 a->materialShape=NULL;
 int fileCount=MapPropAnimationManager_GetAnimListNARCFileCount(anim);
 for(int i=0;i<load->count;i++){
  int id=a->modelIDs[i+1];if(id>=550||a->modelFiles[id])GF_AssertFail();
  a->modelFiles[id]=NARC_AllocAndReadWholeMember(load->models,id,HEAP_ID_FIELD1);
  MapPropAnimListFile desc;NARC_ReadWholeMember(a->animations,id,&desc);
  if(!GF3dRender_BindModelSet(a->modelFiles[id],a->propTexture))GF_AssertFail();
  if(((u8 *)&desc)[3]==0){
   UnkStruct_FieldSysC0_SubC *obj=ov01_022040A4(unkC0,id);
   if(id<fileCount)ov01_021E8F3C(id,FirstModel(a->modelFiles[id]),&obj->renderObj,a->propTexture,&desc,anim,unk104);
  }
 }
 if(!a->modelFiles[0]){
  a->modelFiles[0]=NARC_AllocAndReadWholeMember(load->models,0,HEAP_ID_FIELD1);
  ov01_022040A4(unkC0,0);
  NNSG3dResTex *tex=NNS_G3dGetTex(a->modelFiles[0]);
  if(tex){if(!GF3dRender_AllocAndLoadTexResources(tex))GF_AssertFail();if(!GF3dRender_BindModelSet(a->modelFiles[0],tex))GF_AssertFail();}
 }
 NARC_Delete(load->models);
 G3X_EdgeMarking(TRUE);
 G3X_SetEdgeColorTable(a->area.lighting?ssdata_overlay_01_021FB878_ov01_02208BB0:ssdata_overlay_01_021FB878_ov01_02208BA0);
 a->materialShape=(AreaDataManager_Sub8AC *)ov01_021EA724();
 ov01_021EA73C(a->area.resourceKind?ssdata_overlay_01_021FB878_ov01_02209A88:ssdata_overlay_01_021FB878_ov01_02209AB4,(UnkStruct_Ov01_021EA6C4 *)a->materialShape);
 {extern void VitaNativeMemLog(const char*,...);VitaNativeMemLog("[SS-AREA] loaded models=%u maptex=%u props=%u",load->count,a->area.textures,a->area.props);}
 Heap_Free(load);a->load=NULL;
}
