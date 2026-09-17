/* The DS's overlay modules, as one always-resident program.
 *
 * On the DS the game loads about a hundred code overlays over each other from the ROM as it moves
 * between scenes. Here every overlay's code is linked in and always present; what still has to
 * happen is the part loading did for the game besides moving code: each module's static data starts
 * again from its initial values, and its BSS starts zeroed, every time the game "loads" it. Those
 * per-module sections are laid out by overlays/gen-link.py, and ranges.h is the table of their
 * bounds.
 *
 * The rest of an overlay's identity -- where it lived in DS memory, how big it was -- is metadata
 * the game reads back but never dereferences. That comes from the ROM's own overlay table, read
 * here at startup. The PSP build baked it into the executable at build time, which meant the build
 * needed the ROM; reading it at runtime means one build works with any dump of the same game, and a
 * ROM that does not match is caught at startup with a reason rather than by being wrong later.
 */
#include <nitro.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* printf goes nowhere on a console; the log on the memory card is the only evidence there. */
extern void VitaNativeMemLog(const char*fmt,...);
struct Range {unsigned char *data,*dataEnd,*bss,*bssEnd,*sinit,*sinitEnd;};
#include "ranges.h"
#define COUNT (sizeof(ranges)/sizeof(ranges[0]))
static unsigned char *initialData[COUNT],state[COUNT];
static u32 romBounds[COUNT][3];   /* the DS's ram_address, ram_size and bss_size, from the ROM */
static BOOL initialized;

static u32 Read32(const unsigned char*p){return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}

/* The ARM9 overlay table: 32 bytes per module, at the offset in the ROM header at 0x50, and its
 * length at 0x54. Each record starts with the module's own id, which is what makes this table
 * checkable rather than merely plausible. */
static BOOL ReadOverlayTable(const char*romPath){
 unsigned char header[512],record[32];
 u32 offset,size;
 FILE*rom=fopen(romPath,"rb");
 if(!rom){VitaNativeMemLog("[OVERLAY] cannot open %s",romPath);return FALSE;}
 if(fread(header,1,sizeof header,rom)!=sizeof header){fclose(rom);VitaNativeMemLog("[OVERLAY] %s is too short to be a DS ROM",romPath);return FALSE;}
 offset=Read32(header+0x50);size=Read32(header+0x54);
 if(size!=32u*COUNT){
  fclose(rom);
  VitaNativeMemLog("[OVERLAY] this ROM has %lu overlay modules, the build expects %u: wrong game or region",
                   (unsigned long)(size/32u),(unsigned)COUNT);
  return FALSE;
 }
 if(fseek(rom,(long)offset,SEEK_SET)){fclose(rom);VitaNativeMemLog("[OVERLAY] overlay table at %lu is past the end of the ROM",(unsigned long)offset);return FALSE;}
 for(unsigned i=0;i<COUNT;i++){
  if(fread(record,1,sizeof record,rom)!=sizeof record){fclose(rom);VitaNativeMemLog("[OVERLAY] overlay table ends after %u of %u modules",i,(unsigned)COUNT);return FALSE;}
  if(Read32(record)!=i){fclose(rom);VitaNativeMemLog("[OVERLAY] overlay table record %u says it is module %lu",i,(unsigned long)Read32(record));return FALSE;}
  romBounds[i][0]=Read32(record+4);   /* where it was loaded in DS memory */
  romBounds[i][1]=Read32(record+8);   /* how much code and data */
  romBounds[i][2]=Read32(record+12);  /* how much BSS */
 }
 fclose(rom);
 return TRUE;
}

BOOL VitaNativeOverlay_Init(const char*romPath){
 if(initialized)return TRUE;
 if(!ReadOverlayTable(romPath))return FALSE;
 for(unsigned i=0;i<COUNT;i++){
  size_t n=ranges[i].dataEnd-ranges[i].data;
  if(n){initialData[i]=malloc(n);if(!initialData[i]){for(unsigned j=0;j<i;j++){free(initialData[j]);initialData[j]=NULL;}return FALSE;}memcpy(initialData[i],ranges[i].data,n);}
 }
 initialized=TRUE;return TRUE;
}
BOOL FS_LoadOverlayInfo(FSOverlayInfo *o,MIProcessor target,FSOverlayID id){
 if(!o||target!=MI_PROCESSOR_ARM9||id>=COUNT)return FALSE;
 memset(o,0,sizeof(*o));o->target=target;o->header.id=id;
 /* DS addresses are used only as lifetime and overlap metadata, never dereferenced. */
 o->header.ram_address=(u8*)(uintptr_t)romBounds[id][0];o->header.ram_size=romBounds[id][1];o->header.bss_size=romBounds[id][2];
 return TRUE;
}
BOOL FS_LoadOverlayImage(FSOverlayInfo *o){
 if(!initialized||!o||o->target!=MI_PROCESSOR_ARM9||o->header.id>=COUNT)return FALSE;
 unsigned i=o->header.id;if(state[i])return FALSE;
 /* These SDK-owned module libraries have not yet received native reset sections. */
 if(i==4||i==18||i==60||i==66){VitaNativeMemLog("[OVERLAY] unsupported library lifetime for module %u",i);return FALSE;}
 VitaNativeMemLog("[OVERLAY] load id=%u",i);
 struct Range*r=&ranges[i];memset(r->bss,0,r->bssEnd-r->bss);if(r->dataEnd!=r->data)memcpy(r->data,initialData[i],r->dataEnd-r->data);state[i]=1;return TRUE;
}
void FS_StartOverlay(FSOverlayInfo *o){
 if(!o||o->target!=MI_PROCESSOR_ARM9||o->header.id>=COUNT||state[o->header.id]!=1)abort();
 unsigned i=o->header.id;state[i]=2; /* Constructors can synchronously load another module. */
 for(void (**fn)(void)=(void(**)(void))ranges[i].sinit;fn<(void(**)(void))ranges[i].sinitEnd;fn++)if(*fn)(*fn)();
}
BOOL FS_LoadOverlay(MIProcessor target,FSOverlayID id){FSOverlayInfo o;if(!FS_LoadOverlayInfo(&o,target,id)||!FS_LoadOverlayImage(&o))return FALSE;FS_StartOverlay(&o);return TRUE;}
BOOL FS_UnloadOverlay(MIProcessor target,FSOverlayID id){if(target!=MI_PROCESSOR_ARM9||id>=COUNT||!state[id])return FALSE;state[id]=0;return TRUE;}
BOOL FS_UnloadOverlayImage(FSOverlayInfo *o){return o&&FS_UnloadOverlay(o->target,o->header.id);}
