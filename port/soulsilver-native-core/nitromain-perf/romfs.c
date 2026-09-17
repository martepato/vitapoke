// Read-only native PSP NitroFS backend. All ROM handles are opened "rb".
#include <nitro/fs.h>
#include <nitro/card.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VitaNativeRomFS_SetPath SSRaw_VitaNativeRomFS_SetPath
#define FS_End SSRaw_FS_End
#define FS_Init SSRaw_FS_Init
#define FS_IsAvailable SSRaw_FS_IsAvailable
#define FS_GetDefaultDMA SSRaw_FS_GetDefaultDMA
#define FS_SetDefaultDMA SSRaw_FS_SetDefaultDMA
#define FS_FindArchive SSRaw_FS_FindArchive
#define FS_InitFile SSRaw_FS_InitFile
#define FS_ConvertPathToFileID SSRaw_FS_ConvertPathToFileID
#define FS_OpenFileDirect SSRaw_FS_OpenFileDirect
#define FS_OpenFileFast SSRaw_FS_OpenFileFast
#define FS_OpenFile SSRaw_FS_OpenFile
#define FS_CloseFile SSRaw_FS_CloseFile
#define FS_ReadFile SSRaw_FS_ReadFile
#define FS_ReadFileAsync SSRaw_FS_ReadFileAsync
#define FS_WaitAsync SSRaw_FS_WaitAsync
#define FS_WriteFile SSRaw_FS_WriteFile
#define FS_WriteFileAsync SSRaw_FS_WriteFileAsync
#define FS_SeekFile SSRaw_FS_SeekFile
#define FS_ChangeDir SSRaw_FS_ChangeDir
#define FS_FindDir SSRaw_FS_FindDir
#define FS_ReadDir SSRaw_FS_ReadDir
#define FS_SeekDir SSRaw_FS_SeekDir
#define FS_LoadArchiveTables SSRaw_FS_LoadArchiveTables
#define FS_TryLoadTable SSRaw_FS_TryLoadTable
#define FS_UnloadArchiveTables SSRaw_FS_UnloadArchiveTables
#define CARD_GetRomHeader SSRaw_CARD_GetRomHeader
#define CARD_Init SSRaw_CARD_Init
#define CARD_IsPulledOut SSRaw_CARD_IsPulledOut
static char romPath[512]="Platinum.nds";
static FSArchive archive;
static unsigned char header[512] __attribute__((aligned(4)));
static unsigned char *tables,*fat,*fnt;
static u32 romSize,fatSize,fntSize,dirCount,currentDir,dma;
static BOOL ready;
static FILE *romStream;
/* ---- ROM block cache (see the identical cache in native-audio-app/services/romfs.c) --------------
 * SoulSilver reloads NARC members (e.g. the Bag item icon per cursor step) from the shared ROM handle,
 * re-reading each NARC's BTAF/BTNF/GMIF header every time. This LRU of 16 KiB ROM-aligned blocks turns
 * repeated reads into memcpy. ROM is read-only so blocks never go stale (cleared on FS_End). Absolute
 * offset keyed. SSRaw_FS_ReadFile runs under SSRomLock, so the cache needs no extra locking. Build with
 * -DNO_ROM_CACHE to disable. */
#ifndef NO_ROM_CACHE
#define OPT_ROM_CACHE 1
#define ROM_CACHE_BLOCK  0x4000u
#define ROM_CACHE_BLOCKS 32
static unsigned char romCache[ROM_CACHE_BLOCKS][ROM_CACHE_BLOCK] __attribute__((aligned(16)));
static u32 romCacheTag[ROM_CACHE_BLOCKS];
static u32 romCacheLen[ROM_CACHE_BLOCKS];
static u32 romCacheLRU[ROM_CACHE_BLOCKS];
static u32 romCacheClock;
static int romCacheInit;
#define CACHE_EMPTY 0xffffffffu
static s32 RomCachedRead(u32 pos,void*dst,u32 len){
 if(!romCacheInit){for(int i=0;i<ROM_CACHE_BLOCKS;i++)romCacheTag[i]=CACHE_EMPTY;romCacheInit=1;}
 unsigned char*out=dst;u32 done=0;
 while(done<len){
  u32 p=pos+done,base=p&~(ROM_CACHE_BLOCK-1);
  int slot=-1;
  for(int i=0;i<ROM_CACHE_BLOCKS;i++)if(romCacheTag[i]==base){slot=i;break;}
  if(slot<0){
   slot=0;for(int i=1;i<ROM_CACHE_BLOCKS;i++)if(romCacheLRU[i]<romCacheLRU[slot])slot=i;
   u32 want=ROM_CACHE_BLOCK;if((uint64_t)base+want>romSize)want=romSize>base?romSize-base:0;
   /* SS's original read cleared the stream's sticky EOF/error flag before EVERY read; dropping that made a
      short fread cache a zero-length block and silenced the sound banks on hardware. Clear it here. */
   clearerr(romStream);
   if(fseek(romStream,(long)base,SEEK_SET)){romCacheTag[slot]=CACHE_EMPTY;return done?(s32)done:-1;}
   size_t got=want?fread(romCache[slot],1,want,romStream):0;
   /* Error OR unexpected short read: never cache a poisoned block (want is clamped at the ROM end). */
   if(want&&(ferror(romStream)||got!=want)){clearerr(romStream);romCacheTag[slot]=CACHE_EMPTY;return done?(s32)done:-1;}
   romCacheTag[slot]=base;romCacheLen[slot]=(u32)got;
  }
  romCacheLRU[slot]=++romCacheClock;
  u32 off=p-base;
  if(off>=romCacheLen[slot])break;
  u32 avail=romCacheLen[slot]-off,chunk=len-done;
  if(chunk>avail)chunk=avail;
  memcpy(out+done,romCache[slot]+off,chunk);
  done+=chunk;
  if(romCacheLen[slot]<ROM_CACHE_BLOCK)break;
 }
 return (s32)done;
}
#endif
static u32 streamPos;
typedef struct RomHandle {struct RomHandle *next;FSFile *owner;} RomHandle;
static RomHandle *handles;
static RomHandle *FindHandle(FSFile *f){if(!f)return NULL;for(RomHandle*h=handles;h;h=h->next)if(h->owner==f&&f->pcFilePtr==(FILE*)h)return h;return NULL;}
static void DropHandle(FSFile*f){RomHandle**p=&handles;while(*p){RomHandle*h=*p;if(h->owner==f){*p=h->next;free(h);return;}p=&h->next;}}

static u16 U16(const void*p){const u8*b=p;return b[0]|((u16)b[1]<<8);}
static u32 U32(const void*p){const u8*b=p;return U16(b)|((u32)U16(b+2)<<16);}
BOOL VitaNativeRomFS_SetPath(const char*path){if(ready||!path||strlen(path)>=sizeof(romPath))return FALSE;strcpy(romPath,path);return TRUE;}
void FS_End(void){while(handles){RomHandle*h=handles;handles=h->next;free(h);}if(romStream)fclose(romStream);romStream=NULL;streamPos=0;free(tables);tables=fat=fnt=NULL;ready=FALSE;
#ifdef OPT_ROM_CACHE
 for(int i=0;i<ROM_CACHE_BLOCKS;i++)romCacheTag[i]=CACHE_EMPTY;
#endif
 memset(&archive,0,sizeof(archive));}
void FS_Init(u32 channel){
 dma=channel;if(ready)return;
 FILE*stream=fopen(romPath,"rb");if(!stream)return;
 if(fseek(stream,0,SEEK_END)){fclose(stream);return;}
 long length=ftell(stream);if(length<512){fclose(stream);return;}romSize=(u32)length;
 if(fseek(stream,0,SEEK_SET)||fread(header,1,512,stream)!=512){fclose(stream);return;}
 u32 fo=U32(header+0x40),ao=U32(header+0x48);fntSize=U32(header+0x44);fatSize=U32(header+0x4c);
 if(fntSize<8||fntSize>16*1024*1024||!fatSize||fatSize>65536*8||(fatSize&7)||(uint64_t)fo+fntSize>romSize||(uint64_t)ao+fatSize>romSize){fclose(stream);return;}
 tables=malloc(fatSize+fntSize);if(!tables){fclose(stream);return;}fat=tables;fnt=tables+fatSize;
 if(fseek(stream,ao,SEEK_SET)||fread(fat,1,fatSize,stream)!=fatSize||fseek(stream,fo,SEEK_SET)||fread(fnt,1,fntSize,stream)!=fntSize){fclose(stream);FS_End();return;}
 romStream=stream;streamPos=(u32)ftell(stream);dirCount=U16(fnt+6);
 if(!dirCount||dirCount>4096||dirCount*8>fntSize){FS_End();return;}
 for(u32 i=0;i<fatSize;i+=8)if(U32(fat+i)>U32(fat+i+4)||U32(fat+i+4)>romSize){FS_End();return;}
 memset(&archive,0,sizeof(archive));memcpy(archive.name.ptr,"rom",4);
 archive.fat=archive.fat_bak=ao;archive.fnt=archive.fnt_bak=fo;archive.fat_size=fatSize;archive.fnt_size=fntSize;
 archive.flag=FS_ARCHIVE_FLAG_REGISTER|FS_ARCHIVE_FLAG_LOADED;currentDir=0xf000;ready=TRUE;
}
BOOL FS_IsAvailable(void){return ready;}
u32 FS_GetDefaultDMA(void){return dma;}
u32 FS_SetDefaultDMA(u32 n){u32 old=dma;dma=n;return old;}
FSArchive* FS_FindArchive(const char*name,int len){return ready&&len==3&&!memcmp(name,"rom",3)?&archive:NULL;}
void FS_InitFile(FSFile*f){DropHandle(f);memset(f,0,sizeof(*f));f->command=FS_COMMAND_INVALID;}
static BOOL DirPos(u32 id,FSDirPos*out){
 u32 index=id&4095;if(!ready||id<0xf000||id>0xffff||index>=dirCount)return FALSE;
 u32 pos=U32(fnt+8*index);if(pos>=fntSize)return FALSE;
 out->arc=&archive;out->own_id=id;out->index=U16(fnt+8*index+4);out->pos=pos;return TRUE;
}
static BOOL Next(FSDirPos*pos,FSDirEntry*entry){
 if(pos->pos>=fntSize)return FALSE;u8 code=fnt[pos->pos++];if(!code)return FALSE;
 u32 n=code&127;if(!n||pos->pos+n+((code&128)?2:0)>fntSize)return FALSE;
 memset(entry,0,sizeof(*entry));memcpy(entry->name,fnt+pos->pos,n);entry->name_len=n;pos->pos+=n;
 entry->is_directory=(code&128)!=0;
 if(entry->is_directory){u32 id=U16(fnt+pos->pos);pos->pos+=2;if(!DirPos(id,&entry->dir_id))return FALSE;}
 else {entry->file_id.arc=&archive;entry->file_id.file_id=pos->index++;if(entry->file_id.file_id>=fatSize/8)return FALSE;}
 return TRUE;
}
static BOOL Resolve(const char*path,BOOL directory,u32*result){
 if(!ready||!path)return FALSE;u32 dir=currentDir;
 if(!strncmp(path,"rom:",4)){path+=4;dir=0xf000;}else if(strchr(path,':'))return FALSE;
 if(*path=='/')dir=0xf000;
 for(;;){
  while(*path=='/')path++;
  if(!*path){if(!directory)return FALSE;*result=dir;return TRUE;}
  const char*end=strchr(path,'/');size_t n=end?(size_t)(end-path):strlen(path);
  if(n==1&&path[0]=='.'){path+=n;continue;}
  if(n==2&&!memcmp(path,"..",2)){if(dir!=0xf000)dir=U16(fnt+(dir&4095)*8+6);path+=n;continue;}
  if(!n||n>127)return FALSE;
  FSDirPos pos;if(!DirPos(dir,&pos))return FALSE;FSDirEntry entry;BOOL found=FALSE;
  while(Next(&pos,&entry))if(entry.name_len==n&&!memcmp(entry.name,path,n)){found=TRUE;break;}
  if(!found)return FALSE;path+=n;while(*path=='/')path++;
  if(!*path){if(directory!=entry.is_directory)return FALSE;*result=directory?entry.dir_id.own_id:entry.file_id.file_id;return TRUE;}
  if(!entry.is_directory)return FALSE;dir=entry.dir_id.own_id;
 }
}
BOOL FS_ConvertPathToFileID(FSFileID*id,const char*path){u32 result;if(!id||!Resolve(path,FALSE,&result))return FALSE;id->arc=&archive;id->file_id=result;return TRUE;}
BOOL FS_OpenFileDirect(FSFile*f,FSArchive*a,u32 top,u32 bottom,u32 index){
 if(!ready||!f||f->pcFilePtr||a!=&archive||top>bottom||bottom>romSize)return FALSE;
 RomHandle*h=malloc(sizeof(*h));if(!h)return FALSE;h->owner=f;h->next=handles;handles=h;
 f->pcFilePtr=(FILE*)h;f->arc=a;f->stat=FS_FILE_STATUS_IS_FILE;f->error=FS_RESULT_SUCCESS;
 f->prop.file.own_id=index;f->prop.file.top=top;f->prop.file.bottom=bottom;f->prop.file.pos=top;return TRUE;
}
BOOL FS_OpenFileFast(FSFile*f,FSFileID id){if(!ready||!f||id.arc!=&archive||id.file_id>=fatSize/8)return FALSE;const u8*p=fat+id.file_id*8;return FS_OpenFileDirect(f,&archive,U32(p),U32(p+4),id.file_id);}
BOOL FS_OpenFile(FSFile*f,const char*path){FSFileID id;return FS_ConvertPathToFileID(&id,path)&&FS_OpenFileFast(f,id);}
BOOL FS_CloseFile(FSFile*f){if(!f)return FALSE;DropHandle(f);f->pcFilePtr=NULL;f->stat=0;f->arc=NULL;return TRUE;}
s32 FS_ReadFile(FSFile*f,void*dst,s32 len){
 if(!ready||!FindHandle(f)||!dst||len<0)return -1;
 if(f->prop.file.pos<f->prop.file.top||f->prop.file.pos>f->prop.file.bottom)return -1;
 u32 rest=f->prop.file.bottom-f->prop.file.pos;if((u32)len>rest)len=rest;
 #ifdef OPT_ROM_CACHE
 /* Absolute-offset cache; leaves romStream's real position wherever the last block read ended, so mark
    streamPos unknown (every RomCachedRead miss fseeks anyway). */
 { s32 n=RomCachedRead(f->prop.file.pos,dst,(u32)len); streamPos=0xffffffffu;
   if(n<0){f->error=FS_RESULT_FAILURE;return -1;}
   f->prop.file.pos+=(u32)n; f->error=FS_RESULT_SUCCESS; return n; }
#else
 clearerr(romStream);
 if(streamPos!=f->prop.file.pos){if(fseek(romStream,(long)f->prop.file.pos,SEEK_SET)){f->error=FS_RESULT_FAILURE;return -1;}streamPos=f->prop.file.pos;}
 size_t n=fread(dst,1,len,romStream);f->prop.file.pos+=n;streamPos+=n;
 if(ferror(romStream)){f->error=FS_RESULT_FAILURE;return -1;}f->error=FS_RESULT_SUCCESS;return n;
#endif
}
s32 FS_ReadFileAsync(FSFile*f,void*dst,s32 len){return FS_ReadFile(f,dst,len);}
BOOL FS_WaitAsync(FSFile*f){return ready&&FindHandle(f)&&f->error==FS_RESULT_SUCCESS;}
s32 FS_WriteFile(FSFile*f,const void*src,s32 len){(void)src;(void)len;if(f)f->error=FS_RESULT_UNSUPPORTED;return -1;}
s32 FS_WriteFileAsync(FSFile*f,const void*src,s32 len){return FS_WriteFile(f,src,len);}
BOOL FS_SeekFile(FSFile*f,s32 offset,FSSeekFileMode mode){
 if(!ready||!FindHandle(f))return FALSE;int64_t pos=offset;
 if(mode==FS_SEEK_SET)pos+=f->prop.file.top;else if(mode==FS_SEEK_CUR)pos+=f->prop.file.pos;else if(mode==FS_SEEK_END)pos+=f->prop.file.bottom;else return FALSE;
 if(pos<f->prop.file.top)pos=f->prop.file.top;if(pos>f->prop.file.bottom)pos=f->prop.file.bottom;
 f->prop.file.pos=pos;return TRUE;
}
BOOL FS_ChangeDir(const char*path){u32 id;if(!Resolve(path,TRUE,&id))return FALSE;currentDir=id;return TRUE;}
BOOL FS_FindDir(FSFile*f,const char*path){u32 id;if(!f||!Resolve(path,TRUE,&id))return FALSE;FS_InitFile(f);f->arc=&archive;f->stat=FS_FILE_STATUS_IS_DIR;return DirPos(id,&f->prop.dir.pos);}
BOOL FS_ReadDir(FSFile*f,FSDirEntry*entry){return f&&entry&&(f->stat&FS_FILE_STATUS_IS_DIR)&&Next(&f->prop.dir.pos,entry);}
BOOL FS_SeekDir(FSFile*f,const FSDirPos*pos){if(!f||!pos||pos->arc!=&archive)return FALSE;f->arc=&archive;f->stat=FS_FILE_STATUS_IS_DIR;f->prop.dir.pos=*pos;return TRUE;}
u32 FS_LoadArchiveTables(FSArchive*a,void*memory,u32 size){
 u32 required=fatSize+fntSize;if(!ready||a!=&archive)return 0;
 if(memory&&size>=required){memcpy(memory,tables,required);a->load_mem=memory;a->flag|=FS_ARCHIVE_FLAG_TABLE_LOAD;}
 return required;
}
u32 FS_TryLoadTable(void*memory,u32 size){return FS_LoadArchiveTables(&archive,memory,size);}
void* FS_UnloadArchiveTables(FSArchive*a){if(a!=&archive)return NULL;void*p=a->load_mem;a->load_mem=NULL;a->flag&=~FS_ARCHIVE_FLAG_TABLE_LOAD;return p;}
const u8* CARD_GetRomHeader(void){return ready?header:NULL;}
void CARD_Init(void){
 if(!ready)FS_Init(0);if(!ready)return;
 // Real backing array supplied by the native platform. These are the original
 // bytes; boot.c performs its own separate checkHeader marker update.
 memcpy((void*)(uintptr_t)HW_ROM_HEADER_BUF,header,HW_CARD_ROM_HEADER_SIZE);
 memcpy((void*)(uintptr_t)HW_CARD_ROM_HEADER,header,HW_CARD_ROM_HEADER_SIZE);
}
#include <sys/stat.h>
BOOL CARD_IsPulledOut(void){struct stat st;return stat(romPath,&st)!=0||st.st_size<512;}

extern void SSRomLock(void);
extern void SSRomUnlock(void);
#undef VitaNativeRomFS_SetPath
BOOL VitaNativeRomFS_SetPath(const char*path){SSRomLock();BOOL result=SSRaw_VitaNativeRomFS_SetPath(path);SSRomUnlock();return result;}
#undef FS_End
void FS_End(void){SSRomLock();SSRaw_FS_End();SSRomUnlock();}
#undef FS_Init
void FS_Init(u32 channel){SSRomLock();SSRaw_FS_Init(channel);SSRomUnlock();}
#undef FS_IsAvailable
BOOL FS_IsAvailable(void){SSRomLock();BOOL result=SSRaw_FS_IsAvailable();SSRomUnlock();return result;}
#undef FS_GetDefaultDMA
u32 FS_GetDefaultDMA(void){SSRomLock();u32 result=SSRaw_FS_GetDefaultDMA();SSRomUnlock();return result;}
#undef FS_SetDefaultDMA
u32 FS_SetDefaultDMA(u32 n){SSRomLock();u32 result=SSRaw_FS_SetDefaultDMA(n);SSRomUnlock();return result;}
#undef FS_FindArchive
FSArchive* FS_FindArchive(const char*name,int len){SSRomLock();FSArchive* result=SSRaw_FS_FindArchive(name,len);SSRomUnlock();return result;}
#undef FS_InitFile
void FS_InitFile(FSFile*f){SSRomLock();SSRaw_FS_InitFile(f);SSRomUnlock();}
#undef FS_ConvertPathToFileID
BOOL FS_ConvertPathToFileID(FSFileID*id,const char*path){SSRomLock();BOOL result=SSRaw_FS_ConvertPathToFileID(id,path);SSRomUnlock();return result;}
#undef FS_OpenFileDirect
BOOL FS_OpenFileDirect(FSFile*f,FSArchive*a,u32 top,u32 bottom,u32 index){SSRomLock();BOOL result=SSRaw_FS_OpenFileDirect(f,a,top,bottom,index);SSRomUnlock();return result;}
#undef FS_OpenFileFast
BOOL FS_OpenFileFast(FSFile*f,FSFileID id){SSRomLock();BOOL result=SSRaw_FS_OpenFileFast(f,id);SSRomUnlock();return result;}
#undef FS_OpenFile
BOOL FS_OpenFile(FSFile*f,const char*path){SSRomLock();BOOL result=SSRaw_FS_OpenFile(f,path);SSRomUnlock();return result;}
#undef FS_CloseFile
BOOL FS_CloseFile(FSFile*f){SSRomLock();BOOL result=SSRaw_FS_CloseFile(f);SSRomUnlock();return result;}
#undef FS_ReadFile
s32 FS_ReadFile(FSFile*f,void*dst,s32 len){SSRomLock();s32 result=SSRaw_FS_ReadFile(f,dst,len);SSRomUnlock();return result;}
#undef FS_ReadFileAsync
s32 FS_ReadFileAsync(FSFile*f,void*dst,s32 len){SSRomLock();s32 result=SSRaw_FS_ReadFileAsync(f,dst,len);SSRomUnlock();return result;}
#undef FS_WaitAsync
BOOL FS_WaitAsync(FSFile*f){SSRomLock();BOOL result=SSRaw_FS_WaitAsync(f);SSRomUnlock();return result;}
#undef FS_WriteFile
s32 FS_WriteFile(FSFile*f,const void*src,s32 len){SSRomLock();s32 result=SSRaw_FS_WriteFile(f,src,len);SSRomUnlock();return result;}
#undef FS_WriteFileAsync
s32 FS_WriteFileAsync(FSFile*f,const void*src,s32 len){SSRomLock();s32 result=SSRaw_FS_WriteFileAsync(f,src,len);SSRomUnlock();return result;}
#undef FS_SeekFile
BOOL FS_SeekFile(FSFile*f,s32 offset,FSSeekFileMode mode){SSRomLock();BOOL result=SSRaw_FS_SeekFile(f,offset,mode);SSRomUnlock();return result;}
#undef FS_ChangeDir
BOOL FS_ChangeDir(const char*path){SSRomLock();BOOL result=SSRaw_FS_ChangeDir(path);SSRomUnlock();return result;}
#undef FS_FindDir
BOOL FS_FindDir(FSFile*f,const char*path){SSRomLock();BOOL result=SSRaw_FS_FindDir(f,path);SSRomUnlock();return result;}
#undef FS_ReadDir
BOOL FS_ReadDir(FSFile*f,FSDirEntry*entry){SSRomLock();BOOL result=SSRaw_FS_ReadDir(f,entry);SSRomUnlock();return result;}
#undef FS_SeekDir
BOOL FS_SeekDir(FSFile*f,const FSDirPos*pos){SSRomLock();BOOL result=SSRaw_FS_SeekDir(f,pos);SSRomUnlock();return result;}
#undef FS_LoadArchiveTables
u32 FS_LoadArchiveTables(FSArchive*a,void*memory,u32 size){SSRomLock();u32 result=SSRaw_FS_LoadArchiveTables(a,memory,size);SSRomUnlock();return result;}
#undef FS_TryLoadTable
u32 FS_TryLoadTable(void*memory,u32 size){SSRomLock();u32 result=SSRaw_FS_TryLoadTable(memory,size);SSRomUnlock();return result;}
#undef FS_UnloadArchiveTables
void* FS_UnloadArchiveTables(FSArchive*a){SSRomLock();void* result=SSRaw_FS_UnloadArchiveTables(a);SSRomUnlock();return result;}
#undef CARD_GetRomHeader
const u8* CARD_GetRomHeader(void){SSRomLock();const u8* result=SSRaw_CARD_GetRomHeader();SSRomUnlock();return result;}
#undef CARD_Init
void CARD_Init(void){SSRomLock();SSRaw_CARD_Init();SSRomUnlock();}
#undef CARD_IsPulledOut
BOOL CARD_IsPulledOut(void){SSRomLock();BOOL result=SSRaw_CARD_IsPulledOut();SSRomUnlock();return result;}
