/* Resident native code, with actual SoulSilver overlay data lifetimes.
 * DS RAM addresses below are metadata only and are never dereferenced. */
#include <nitro.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
struct NativeRange {unsigned char *data,*dataEnd,*bss,*bssEnd;};
struct RomBounds {u32 address,size,bss,fileID;BOOL supported;};
#include "ranges.h"
#include "rom_bounds.h"
#define COUNT (sizeof(ranges)/sizeof(ranges[0]))
static unsigned char *initial[COUNT],state[COUNT];
static BOOL initialized;
BOOL VitaNativeOverlay_Init(void){
 if(initialized)return TRUE;
 for(unsigned i=0;i<COUNT;i++){
  size_t size=ranges[i].dataEnd-ranges[i].data;
  if(size){initial[i]=malloc(size);if(!initial[i]){for(unsigned j=0;j<i;j++){free(initial[j]);initial[j]=NULL;}return FALSE;}memcpy(initial[i],ranges[i].data,size);}
 }
 initialized=TRUE;return TRUE;
}
/* Modules the registry rejects for having ROM static-constructor entries, handled natively:
 *  12  battle engine - its only .sinit entry is ov12_022399BC: load OVY_18 unless sub_02039998()
 *      (run below in FS_StartOverlay);
 * 123  ds_protect - five DSProt constructors; every DSProt call is answered by dsprot_native.c,
 *      so the module has no state to reset and its constructors are never needed. */
extern BOOL sub_02039998(void);extern BOOL HandleLoadOverlay(FSOverlayID id,int loadType);
static BOOL Supported(unsigned id){return bounds[id].supported||id==12||id==123;}
static void NativeStaticInit(unsigned id){
 if(id==12&&!sub_02039998()){extern void VitaNativeMemLog(const char*,...);VitaNativeMemLog("[SS-OVERLAY] ov12 sinit: loading OVY_18");HandleLoadOverlay(18,2);}
}
static BOOL Valid(const FSOverlayInfo *o){
 if(!o||o->target!=MI_PROCESSOR_ARM9||o->header.id>=COUNT)return FALSE;
 const struct RomBounds*b=&bounds[o->header.id];
 return (uintptr_t)o->header.ram_address==b->address&&o->header.ram_size==b->size&&o->header.bss_size==b->bss&&o->header.file_id==b->fileID;
}
BOOL FS_LoadOverlayInfo(FSOverlayInfo *o,MIProcessor target,FSOverlayID id){
 if(!o||target!=MI_PROCESSOR_ARM9||id>=COUNT)return FALSE;
 memset(o,0,sizeof(*o));o->target=target;o->header.id=id;
 o->header.ram_address=(u8*)(uintptr_t)bounds[id].address;o->header.ram_size=bounds[id].size;o->header.bss_size=bounds[id].bss;o->header.file_id=bounds[id].fileID;
 // The supported modules have only a NULL static-init terminator in the ROM.
 return TRUE;
}
BOOL FS_LoadOverlayImage(FSOverlayInfo *o){
 if(!initialized||!Valid(o))return FALSE;
 unsigned id=o->header.id;
 if(!Supported(id)){printf("[SS-OVERLAY] unsupported SDK lifetime or constructor id=%u\n",id);return FALSE;}
 if(state[id])return FALSE;
 const struct NativeRange*r=&ranges[id];
 if(r->dataEnd!=r->data)memcpy(r->data,initial[id],r->dataEnd-r->data);
 if(r->bssEnd!=r->bss)memset(r->bss,0,r->bssEnd-r->bss);
 state[id]=1;{extern void VitaNativeMemLog(const char*,...);VitaNativeMemLog("[SS-OVERLAY] native load id=%u data=%u bss=%u",id,(unsigned)(r->dataEnd-r->data),(unsigned)(r->bssEnd-r->bss));}return TRUE;
}
BOOL FS_LoadOverlayImageAsync(FSOverlayInfo *o,FSFile *file){
 if(!file||file->pcFilePtr)return FALSE;
 // Native reset completes synchronously. Expose that completed result to WaitAsync.
 BOOL ok=FS_LoadOverlayImage(o);file->error=ok?FS_RESULT_SUCCESS:FS_RESULT_FAILURE;file->command=FS_COMMAND_READFILE;return ok;
}
void FS_StartOverlay(FSOverlayInfo *o){
 if(!Valid(o)||state[o->header.id]!=1){puts("[SS-OVERLAY] invalid start");abort();}
 state[o->header.id]=2;NativeStaticInit(o->header.id);
}
void FS_EndOverlay(FSOverlayInfo *o){
 if(!Valid(o)||state[o->header.id]!=2){puts("[SS-OVERLAY] invalid end");abort();}
 state[o->header.id]=1;
}
BOOL FS_UnloadOverlayImage(FSOverlayInfo *o){
 if(!Valid(o)||!state[o->header.id])return FALSE;state[o->header.id]=0;return TRUE;
}
BOOL FS_LoadOverlay(MIProcessor target,FSOverlayID id){FSOverlayInfo o;if(!FS_LoadOverlayInfo(&o,target,id)||!FS_LoadOverlayImage(&o))return FALSE;FS_StartOverlay(&o);return TRUE;}
BOOL FS_UnloadOverlay(MIProcessor target,FSOverlayID id){FSOverlayInfo o;if(!FS_LoadOverlayInfo(&o,target,id)||!state[id])return FALSE;if(state[id]==2)FS_EndOverlay(&o);return FS_UnloadOverlayImage(&o);}
FSFileID FS_GetOverlayFileID(const FSOverlayInfo *o){FSFileID id={0};if(Valid(o)){id.arc=FS_FindArchive("rom",3);id.file_id=o->header.file_id;}return id;}
void FS_ClearOverlayImage(FSOverlayInfo *o){
 if(!Valid(o)||!Supported(o->header.id)){puts("[SS-OVERLAY] invalid clear");abort();}
 const struct NativeRange*r=&ranges[o->header.id];if(r->bssEnd!=r->bss)memset(r->bss,0,r->bssEnd-r->bss);
}
