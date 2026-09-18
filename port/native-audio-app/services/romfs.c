// Read-only NitroFS backend: the game's file system, from one of two places.
//
// The game asks for its data by name and only by name -- eleven direct FS_OpenFile calls, and all
// 2621 NARC_* calls through one table of paths -- so there are two sensible ways to give it to it,
// and this serves both:
//
//   bundled  The files were unpacked from a ROM at build time (scripts/extract_assets.py) and packed
//            into the VPK, so they are files under app0: with the paths the game asks for. Nothing
//            has to be on the memory card and nothing is read from a ROM at run time. This is what
//            `./build.sh game --rom FILE` produces, and it is picked automatically when the
//            application has an app0:nitrofs.idx beside it.
//   card     No assets were bundled, so the ROM itself is on the memory card and the file system is
//            served out of it through its own name and allocation tables -- which is what the PSP
//            build did, and what this port did before the assets could be bundled.
//
// Everything below the two Acquire paths is shared: the same path resolution, the same seek-then-read
// discipline, the same statistics.
//
// All handles are opened "rb".
#include <nitro/fs.h>
#include <nitro/card.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void VitaNativeMemLog(const char*fmt,...);
static char romPath[512]="Platinum.nds";
/* One shared stdio handle for the whole ROM.
 *
 * Every FS_OpenFile* used to fopen() the 128 MB ROM again, so the number of DS
 * "files" the game holds open at once became the number of host descriptors
 * held open at once. A console has a small, hard limit on simultaneously open
 * files on its memory card, and past it fopen() simply fails and the caller
 * gets a file it thinks is valid. (The PSP build found this the hard way, on
 * hardware: an emulator does not care.) Entering the field map opens
 * more archives at once than any earlier scene, which matches where hardware
 * dies. Each FSFile now carries only its own position and seeks before it
 * reads, so the descriptor count is one no matter how many files are open. */
static FILE*romStream;
static unsigned openFiles,openFilesHigh,openFailures,readFailures;

/* ---- bundled assets --------------------------------------------------------------------------
 *
 * nitrofs.idx is written by scripts/extract_assets.py beside the unpacked files: the cartridge
 * header, the ARM9 overlay table, and every file's path by file id. See that script for the layout.
 *
 * The handle pool is the reason this is not simply an fopen per file. A console has a small, hard
 * limit on how many files may be open on its memory card at once, and past it fopen() fails and the
 * caller gets a file it believes is valid -- the PSP build found that on hardware, in the field map,
 * which opens more archives at once than any other scene. Serving every DS file from one handle is
 * how that was avoided before; bundled files cannot do that, because they are different files. So
 * the number of host handles is bounded here instead: at most POOL_HANDLES are open whatever the
 * game does, the least recently read is closed to make room, and every FSFile seeks before it reads
 * because the handle under it may have been reopened since. The counters below say whether the
 * bound is ever reached -- poolEvictions staying at zero means the pool is bigger than the game's
 * working set and nothing is being reopened. */
#define POOL_HANDLES 8
static struct {u32 id;FILE*fp;u32 len;u32 lru;} pool[POOL_HANDLES];
static u32 poolClock,poolHits,poolMisses,poolEvictions,poolOpenFail;
static int bundled;                    /* serving from app0: rather than from a ROM */
static unsigned char*idxBlob;          /* nitrofs.idx, kept for the paths it holds */
static const char**paths;              /* one per file id; NULL where the ROM had no name */
static u32 pathCount;
static const unsigned char*overlayTable;
static u32 overlayBytes;
/* A non-null value for FSFile::pcFilePtr in bundled mode, where there is no one handle to put
 * there. It marks the file open and is never dereferenced. */
static int openMarker;
#define BUNDLED_OPEN ((FILE*)&openMarker)
static FSArchive archive;
static unsigned char header[512] __attribute__((aligned(4)));
static unsigned char *tables,*fat,*fnt;
static u32 romSize,fatSize,fntSize,dirCount,currentDir,dma;
static BOOL ready;

/* ---- ROM block cache -------------------------------------------------------------------------
 * Every FS_ReadFile is an fseek()+fread() on the one shared 128 MB ROM handle. On a real Memory
 * Stick a seek dominates, and the game re-reads the same bytes constantly: each NARC member load
 * re-parses that NARC's BTAF/BTNF/GMIF header with 5-6 tiny (2-4 byte) reads at fixed offsets, so
 * scrolling the Bag (one item-icon NARC member per cursor step) costs ~6-8 seeks per step, most of
 * them re-reading the same header bytes. This LRU cache of 16 KiB ROM-aligned blocks turns those
 * repeated reads into memcpy: the NARC header lands in one block, and a small archive (item icons)
 * fits in a handful of blocks, so a revisited item needs no ROM access at all. Absolute-offset
 * keyed, independent of any FSFile position; all ROM access is on the game thread (the SAS audio
 * thread only mixes), so no new locking is needed. Build with -DNO_ROM_CACHE to disable. */
#ifndef NO_ROM_CACHE
#define OPT_ROM_CACHE 1
#define ROM_CACHE_BLOCK  0x4000u                   /* 16 KiB, aligned to absolute ROM offset */
#define ROM_CACHE_BLOCKS 32                         /* 32 * 16 KiB = 512 KiB, static */
static unsigned char romCache[ROM_CACHE_BLOCKS][ROM_CACHE_BLOCK] __attribute__((aligned(16)));
static u32 romCacheTag[ROM_CACHE_BLOCKS];
static u32 romCacheLen[ROM_CACHE_BLOCKS];
static u32 romCacheLRU[ROM_CACHE_BLOCKS];
static u32 romCacheClock;
static int romCacheInit;
#define CACHE_EMPTY 0xffffffffu
static unsigned st_readCalls,st_blockHit,st_blockMiss;
static unsigned long long st_readBytes;
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
   /* A sticky EOF/error flag left by an earlier read must not short-circuit this one (the original
      per-read code cleared it; on SoulSilver dropping this silenced the sound banks). */
   clearerr(romStream);
   if(fseek(romStream,(long)base,SEEK_SET)){romCacheTag[slot]=CACHE_EMPTY;return done?(s32)done:-1;}
   size_t got=want?fread(romCache[slot],1,want,romStream):0;
   /* Error OR an unexpected short read: never cache a poisoned block (want is already clamped at the
      ROM end, so a full read always has got == want). */
   if(want&&(ferror(romStream)||got!=want)){clearerr(romStream);romCacheTag[slot]=CACHE_EMPTY;return done?(s32)done:-1;}
   romCacheTag[slot]=base;romCacheLen[slot]=(u32)got;st_blockMiss++;
  }else st_blockHit++;
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
static u16 U16(const void*p){const u8*b=p;return b[0]|((u16)b[1]<<8);}
static u32 U32(const void*p){const u8*b=p;return U16(b)|((u32)U16(b+2)<<16);}

/* Every rejection below says why. The game's own reaction to a file system that did not start is to
 * stop dead (CheckForMemoryTampering calls OS_Terminate), which tells whoever is holding the console
 * nothing at all; these lines are the difference between "it does not work" and "your dump is
 * truncated". */
#define FS_REJECT(...) do { VitaNativeMemLog("[ROMFS] " __VA_ARGS__); } while (0)

/* Acquire : the open handle for a bundled file, opening it and making room if need be. Returns NULL
 * and counts the failure if the file will not open; *len gets its size. */
static FILE*Acquire(u32 id,u32*len){
 if(id>=pathCount||!paths[id])return NULL;
 int slot=-1;
 for(int i=0;i<POOL_HANDLES;i++)if(pool[i].fp&&pool[i].id==id){slot=i;poolHits++;break;}
 if(slot<0){
  poolMisses++;
  for(int i=0;i<POOL_HANDLES;i++)if(!pool[i].fp){slot=i;break;}
  if(slot<0){
   slot=0;for(int i=1;i<POOL_HANDLES;i++)if(pool[i].lru<pool[slot].lru)slot=i;
   fclose(pool[slot].fp);pool[slot].fp=NULL;poolEvictions++;
  }
  char full[640];snprintf(full,sizeof full,"app0:%s",paths[id]);
  FILE*fp=fopen(full,"rb");
  if(!fp){poolOpenFail++;if(poolOpenFail<=8)VitaNativeMemLog("[ROMFS] cannot open %s (failure #%u)",full,poolOpenFail);return NULL;}
  long size=0;
  if(!fseek(fp,0,SEEK_END)){size=ftell(fp);if(size<0)size=0;}
  pool[slot].id=id;pool[slot].fp=fp;pool[slot].len=(u32)size;
 }
 pool[slot].lru=++poolClock;
 if(len)*len=pool[slot].len;
 return pool[slot].fp;
}

/* BundledInit : read nitrofs.idx and build the path table. FALSE means "serve from a ROM instead" --
 * silently when there is simply no index, with a reason when there is one and it is unusable. */
static BOOL BundledInit(void){
 FILE*f=fopen("app0:nitrofs.idx","rb");
 if(!f)return FALSE;                                  /* no assets bundled: not an error */
 long size=0;
 if(fseek(f,0,SEEK_END)||(size=ftell(f))<(long)(16+512)){fclose(f);FS_REJECT("nitrofs.idx is %ld bytes: too short",size);return FALSE;}
 idxBlob=malloc((size_t)size);
 if(!idxBlob){fclose(f);FS_REJECT("out of memory for a %ld byte nitrofs.idx",size);return FALSE;}
 BOOL read=!fseek(f,0,SEEK_SET)&&fread(idxBlob,1,(size_t)size,f)==(size_t)size;
 fclose(f);
 if(!read){FS_REJECT("nitrofs.idx ends early");goto fail;}
 if(memcmp(idxBlob,"VPOKFS1",8)){FS_REJECT("nitrofs.idx is not the format this build reads");goto fail;}

 {
  u32 count=U32(idxBlob+8),ovl=U32(idxBlob+12),at=16;
  if(!count||count>65536||(unsigned long long)at+512+ovl>(unsigned long long)size){
   FS_REJECT("nitrofs.idx claims %lu files and %lu overlay bytes in %ld",(unsigned long)count,(unsigned long)ovl,size);goto fail;}
  memcpy(header,idxBlob+at,512);at+=512;
  overlayTable=idxBlob+at;overlayBytes=ovl;at+=ovl;
  paths=calloc(count,sizeof *paths);
  if(!paths){FS_REJECT("out of memory for %lu file paths",(unsigned long)count);goto fail;}
  /* Each path is a length and that many bytes, with no terminator, so each is copied out into a
   * string of its own. Three hundred short strings is nothing, and the alternative -- writing
   * terminators into the blob -- would overwrite the next length field. */
  for(u32 i=0;i<count;i++){
   if((unsigned long long)at+2>(unsigned long long)size){FS_REJECT("nitrofs.idx ends inside its path table at file %lu",(unsigned long)i);goto fail;}
   u32 n=U16(idxBlob+at);at+=2;
   if((unsigned long long)at+n>(unsigned long long)size){FS_REJECT("path %lu runs past the end of nitrofs.idx",(unsigned long)i);goto fail;}
   if(n){
    char*copy=malloc(n+1);
    if(!copy){FS_REJECT("out of memory for path %lu",(unsigned long)i);goto fail;}
    memcpy(copy,idxBlob+at,n);copy[n]=0;paths[i]=copy;at+=n;
   }
  }
  pathCount=count;
 }
 bundled=1;
 return TRUE;

fail:
 if(paths){for(u32 i=0;i<pathCount;i++)free((void*)paths[i]);free(paths);paths=NULL;}
 free(idxBlob);idxBlob=NULL;overlayTable=NULL;overlayBytes=0;pathCount=0;
 return FALSE;
}

/* The ARM9 overlay table, for overlays/overlay.c. 32 bytes a module, or NULL if it is not known. */
const unsigned char*VitaNativeRomFS_OverlayTable(u32*bytes){
 if(bytes)*bytes=overlayBytes;
 return overlayBytes?overlayTable:NULL;
}

BOOL VitaNativeRomFS_SetPath(const char*path){if(ready||!path||strlen(path)>=sizeof(romPath))return FALSE;strcpy(romPath,path);return TRUE;}
void FS_End(void){free(tables);tables=fat=fnt=NULL;ready=FALSE;if(romStream){fclose(romStream);romStream=NULL;}
 /* In a bundled build the table points into idxBlob and is freed with it; from a ROM it is its own
  * allocation. */
 if(!bundled)free((void*)overlayTable);
 for(int i=0;i<POOL_HANDLES;i++)if(pool[i].fp){fclose(pool[i].fp);pool[i].fp=NULL;}
 if(paths){for(u32 i=0;i<pathCount;i++)free((void*)paths[i]);free(paths);paths=NULL;}
 free(idxBlob);idxBlob=NULL;overlayTable=NULL;overlayBytes=0;pathCount=0;bundled=0;
#ifdef OPT_ROM_CACHE
 for(int i=0;i<ROM_CACHE_BLOCKS;i++)romCacheTag[i]=CACHE_EMPTY;
#endif
 memset(&archive,0,sizeof(archive));}
/* Bring the file system up: the bundled assets if this build has them, otherwise the ROM. */
void FS_Init(u32 channel){
 dma=channel;if(ready)return;
 if(BundledInit()){
  memset(&archive,0,sizeof(archive));memcpy(archive.name.ptr,"rom",4);
  archive.flag=FS_ARCHIVE_FLAG_REGISTER|FS_ARCHIVE_FLAG_LOADED;currentDir=0xf000;ready=TRUE;
  VitaNativeMemLog("[ROMFS] %lu files bundled with this build, %u handles",(unsigned long)pathCount,POOL_HANDLES);
  return;
 }
 FILE*stream=fopen(romPath,"rb");if(!stream){FS_REJECT("cannot open %s",romPath);return;}
 if(fseek(stream,0,SEEK_END)){fclose(stream);FS_REJECT("cannot seek in %s",romPath);return;}
 long length=ftell(stream);if(length<512){fclose(stream);FS_REJECT("%s is %ld bytes: too short to be a DS ROM",romPath,length);return;}romSize=(u32)length;
 if(fseek(stream,0,SEEK_SET)||fread(header,1,512,stream)!=512){fclose(stream);FS_REJECT("cannot read the header of %s",romPath);return;}
 u32 fo=U32(header+0x40),ao=U32(header+0x48);fntSize=U32(header+0x44);fatSize=U32(header+0x4c);
 if(fntSize<8||fntSize>16*1024*1024||!fatSize||fatSize>65536*8||(fatSize&7)||(uint64_t)fo+fntSize>romSize||(uint64_t)ao+fatSize>romSize){fclose(stream);FS_REJECT("file tables out of range: names at %lu+%lu, files at %lu+%lu, ROM is %lu bytes",(unsigned long)fo,(unsigned long)fntSize,(unsigned long)ao,(unsigned long)fatSize,(unsigned long)romSize);return;}
 tables=malloc(fatSize+fntSize);if(!tables){fclose(stream);FS_REJECT("out of memory for %lu bytes of file tables",(unsigned long)(fatSize+fntSize));return;}fat=tables;fnt=tables+fatSize;
 if(fseek(stream,ao,SEEK_SET)||fread(fat,1,fatSize,stream)!=fatSize||fseek(stream,fo,SEEK_SET)||fread(fnt,1,fntSize,stream)!=fntSize){fclose(stream);FS_End();FS_REJECT("%s ends inside its own file tables",romPath);return;}
 /* The ARM9 overlay table, which overlays/overlay.c asks for through VitaNativeRomFS_OverlayTable.
  * A bundled build has it in nitrofs.idx; here it comes out of the ROM, so that both ways of
  * serving the data answer the same question the same way. */
 {
  u32 oo=U32(header+0x50),ol=U32(header+0x54);
  if(ol&&!(ol%32)&&(unsigned long long)oo+ol<=(unsigned long long)romSize){
   unsigned char*table=malloc(ol);
   if(table&&!fseek(stream,(long)oo,SEEK_SET)&&fread(table,1,ol,stream)==ol){overlayTable=table;overlayBytes=ol;}
   else{free(table);FS_REJECT("cannot read the overlay table at %lu+%lu",(unsigned long)oo,(unsigned long)ol);}
  }else FS_REJECT("the overlay table at %lu is %lu bytes, which is not a whole number of modules",(unsigned long)oo,(unsigned long)ol);
 }
 dirCount=U16(fnt+6);romStream=stream;
 if(!dirCount||dirCount>4096||dirCount*8>fntSize){FS_End();FS_REJECT("the name table claims %lu directories in %lu bytes",(unsigned long)dirCount,(unsigned long)fntSize);return;}
 for(u32 i=0;i<fatSize;i+=8)if(U32(fat+i)>U32(fat+i+4)||U32(fat+i+4)>romSize){FS_End();FS_REJECT("file %lu runs from %lu to %lu, outside a %lu byte ROM",(unsigned long)(i/8),(unsigned long)U32(fat+i),(unsigned long)U32(fat+i+4),(unsigned long)romSize);return;}
 memset(&archive,0,sizeof(archive));memcpy(archive.name.ptr,"rom",4);
 archive.fat=archive.fat_bak=ao;archive.fnt=archive.fnt_bak=fo;archive.fat_size=fatSize;archive.fnt_size=fntSize;
 archive.flag=FS_ARCHIVE_FLAG_REGISTER|FS_ARCHIVE_FLAG_LOADED;currentDir=0xf000;ready=TRUE;
}
BOOL FS_IsAvailable(void){return ready;}
u32 FS_GetDefaultDMA(void){return dma;}
u32 FS_SetDefaultDMA(u32 n){u32 old=dma;dma=n;return old;}
FSArchive* FS_FindArchive(const char*name,int len){return ready&&len==3&&!memcmp(name,"rom",3)?&archive:NULL;}
void FS_InitFile(FSFile*f){memset(f,0,sizeof(*f));f->command=FS_COMMAND_INVALID;}
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
/* Strip what the DS allows in front of a path but a bundled file's name does not carry. */
static const char*Bare(const char*path){
 if(!strncmp(path,"rom:",4))path+=4;
 while(*path=='/')path++;
 while(path[0]=='.'&&path[1]=='/')path+=2;
 return path;
}
static int SameName(const char*a,const char*b){
 for(;*a&&*b;a++,b++){
  char x=*a,y=*b;
  if(x>='A'&&x<='Z')x=(char)(x-'A'+'a');
  if(y>='A'&&y<='Z')y=(char)(y-'A'+'a');
  if(x!=y)return 0;
 }
 return !*a&&!*b;
}
static BOOL Resolve(const char*path,BOOL directory,u32*result){
 if(!ready||!path)return FALSE;u32 dir=currentDir;
 if(bundled){
  /* There is no directory tree here, only names -- the game never asks for one (no FS_ChangeDir,
   * FS_ReadDir or FS_OpenDirectory anywhere in it), so a directory lookup is refused rather than
   * faked. Names are compared without case, as libntr's own portable path does, because a few of
   * the game's paths differ from the ROM's in case. */
  if(directory)return FALSE;
  const char*want=Bare(path);
  for(u32 i=0;i<pathCount;i++)if(paths[i]&&SameName(paths[i],want)){*result=i;return TRUE;}
  return FALSE;
 }
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
 if(!romStream){openFailures++;VitaNativeMemLog("[ROMFS] no rom stream (open #%u) index=%u",openFailures,(unsigned)index);return FALSE;}
 openFiles++;if(openFiles>openFilesHigh){openFilesHigh=openFiles;if(!(openFilesHigh%8))VitaNativeMemLog("[ROMFS] concurrent open files high water %u",openFilesHigh);}
 f->pcFilePtr=romStream;f->arc=a;f->stat=FS_FILE_STATUS_IS_FILE;f->error=FS_RESULT_SUCCESS;
 f->prop.file.own_id=index;f->prop.file.top=top;f->prop.file.bottom=bottom;f->prop.file.pos=top;return TRUE;
}
BOOL FS_OpenFileFast(FSFile*f,FSFileID id){
 if(!ready||!f||id.arc!=&archive)return FALSE;
 if(bundled){
  u32 len=0;
  if(f->pcFilePtr)return FALSE;
  if(!Acquire(id.file_id,&len)){openFailures++;return FALSE;}
  openFiles++;if(openFiles>openFilesHigh){openFilesHigh=openFiles;if(!(openFilesHigh%8))VitaNativeMemLog("[ROMFS] concurrent open files high water %u",openFilesHigh);}
  f->pcFilePtr=BUNDLED_OPEN;f->arc=&archive;f->stat=FS_FILE_STATUS_IS_FILE;f->error=FS_RESULT_SUCCESS;
  f->prop.file.own_id=id.file_id;f->prop.file.top=0;f->prop.file.bottom=len;f->prop.file.pos=0;
  return TRUE;
 }
 if(id.file_id>=fatSize/8)return FALSE;
 const u8*p=fat+id.file_id*8;return FS_OpenFileDirect(f,&archive,U32(p),U32(p+4),id.file_id);
}
BOOL FS_OpenFile(FSFile*f,const char*path){FSFileID id;return FS_ConvertPathToFileID(&id,path)&&FS_OpenFileFast(f,id);}
BOOL FS_CloseFile(FSFile*f){if(!f)return FALSE;if(f->pcFilePtr&&openFiles)openFiles--;f->pcFilePtr=NULL;f->stat=0;f->arc=NULL;return TRUE;}
s32 FS_ReadFile(FSFile*f,void*dst,s32 len){
 if(!f||!f->pcFilePtr||!dst||len<0)return -1;
 u32 rest=f->prop.file.bottom-f->prop.file.pos;if((u32)len>rest)len=rest;
 if(bundled){
  /* Seek before reading: the pool may have closed and reopened this file for somebody else since
   * the last read, and another FSFile may share the handle's position. */
  FILE*fp=Acquire(f->prop.file.own_id,NULL);
  if(!fp){readFailures++;f->error=FS_RESULT_FAILURE;return -1;}
  clearerr(fp);
  if(fseek(fp,(long)f->prop.file.pos,SEEK_SET)){readFailures++;VitaNativeMemLog("[ROMFS] seek failed in file %u at %u (failure #%u)",(unsigned)f->prop.file.own_id,(unsigned)f->prop.file.pos,readFailures);f->error=FS_RESULT_FAILURE;return -1;}
  size_t got=fread(dst,1,(size_t)len,fp);
  f->prop.file.pos+=(u32)got;
  if((s32)got!=len){readFailures++;VitaNativeMemLog("[ROMFS] short read %u/%d in file %u (failure #%u)",(unsigned)got,(int)len,(unsigned)f->prop.file.own_id,readFailures);}
  f->error=FS_RESULT_SUCCESS;return (s32)got;
 }
#ifdef OPT_ROM_CACHE
 st_readCalls++;st_readBytes+=(unsigned)len;
#ifdef ROMFS_MEASURE
 if((st_readCalls%500)==0)VitaNativeMemLog("[ROMFS-CACHE] calls=%u romreads=%u hits=%u bytes=%llu",st_readCalls,st_blockMiss,st_blockHit,st_readBytes);
#endif
 /* The handle is shared, so a byte range is only valid by absolute offset; the cache keys on that. */
 s32 n=RomCachedRead(f->prop.file.pos,dst,(u32)len);
 if(n<0){readFailures++;VitaNativeMemLog("[ROMFS] read failed pos=%u (failure #%u)",(unsigned)f->prop.file.pos,readFailures);f->error=FS_RESULT_FAILURE;return -1;}
 f->prop.file.pos+=(u32)n;
 if(n!=len){readFailures++;VitaNativeMemLog("[ROMFS] short read %u/%d at %u (failure #%u)",(unsigned)n,(int)len,(unsigned)(f->prop.file.pos-n),readFailures);}
 f->error=FS_RESULT_SUCCESS;return n;
#else
 /* The handle is shared, so this file's position is only true right now. */
 if(fseek(f->pcFilePtr,(long)f->prop.file.pos,SEEK_SET)){readFailures++;VitaNativeMemLog("[ROMFS] seek failed pos=%u (failure #%u)",(unsigned)f->prop.file.pos,readFailures);f->error=FS_RESULT_FAILURE;return -1;}
 size_t n=fread(dst,1,len,f->pcFilePtr);f->prop.file.pos+=n;
 if((s32)n!=len){/* Report, but keep the old contract: partial reads still return n. */
  readFailures++;VitaNativeMemLog("[ROMFS] short read %u/%d at %u err=%d (failure #%u)",(unsigned)n,(int)len,(unsigned)(f->prop.file.pos-n),ferror(f->pcFilePtr),readFailures);clearerr(f->pcFilePtr);}
 if(ferror(f->pcFilePtr)){f->error=FS_RESULT_FAILURE;return -1;}
 f->error=FS_RESULT_SUCCESS;return n;
#endif
}
s32 FS_ReadFileAsync(FSFile*f,void*dst,s32 len){return FS_ReadFile(f,dst,len);}
BOOL FS_WaitAsync(FSFile*f){return f&&f->error==FS_RESULT_SUCCESS;}
s32 FS_WriteFile(FSFile*f,const void*src,s32 len){(void)src;(void)len;if(f)f->error=FS_RESULT_UNSUPPORTED;return -1;}
s32 FS_WriteFileAsync(FSFile*f,const void*src,s32 len){return FS_WriteFile(f,src,len);}
BOOL FS_SeekFile(FSFile*f,s32 offset,FSSeekFileMode mode){
 if(!f||!f->pcFilePtr)return FALSE;int64_t pos=offset;
 if(mode==FS_SEEK_SET)pos+=f->prop.file.top;else if(mode==FS_SEEK_CUR)pos+=f->prop.file.pos;else if(mode==FS_SEEK_END)pos+=f->prop.file.bottom;else return FALSE;
 if(pos<f->prop.file.top)pos=f->prop.file.top;if(pos>f->prop.file.bottom)pos=f->prop.file.bottom;
 f->prop.file.pos=pos;return TRUE;
}
BOOL FS_ChangeDir(const char*path){u32 id;if(!Resolve(path,TRUE,&id))return FALSE;currentDir=id;return TRUE;}
BOOL FS_FindDir(FSFile*f,const char*path){u32 id;if(!f||!Resolve(path,TRUE,&id))return FALSE;FS_InitFile(f);f->arc=&archive;f->stat=FS_FILE_STATUS_IS_DIR;return DirPos(id,&f->prop.dir.pos);}
BOOL FS_ReadDir(FSFile*f,FSDirEntry*entry){return f&&entry&&(f->stat&FS_FILE_STATUS_IS_DIR)&&Next(&f->prop.dir.pos,entry);}
BOOL FS_SeekDir(FSFile*f,const FSDirPos*pos){if(!f||!pos||pos->arc!=&archive)return FALSE;f->arc=&archive;f->stat=FS_FILE_STATUS_IS_DIR;f->prop.dir.pos=*pos;return TRUE;}
u32 FS_LoadArchiveTables(FSArchive*a,void*memory,u32 size){
 u32 required=fatSize+fntSize;if(!ready||a!=&archive||bundled)return 0;
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
BOOL CARD_IsPulledOut(void){
 if(bundled)return FALSE;                 /* the data is inside the application */
 struct stat st;return stat(romPath,&st)!=0||st.st_size<512;
}

void VitaNativeRomFSStats(unsigned*open,unsigned*high,unsigned*openFail,unsigned*readFail){
 if(open)*open=openFiles;if(high)*high=openFilesHigh;if(openFail)*openFail=openFailures;if(readFail)*readFail=readFailures;}
void VitaNativeRomFSCacheStats(unsigned*calls,unsigned*hits,unsigned*misses){
 if(bundled){
  /* The same question in the bundled world: how often did a read find what it needed already open,
   * and how often did the pool have to go back to the card. An eviction count of zero means the
   * pool is never the bottleneck. */
  if(calls)*calls=poolHits+poolMisses;if(hits)*hits=poolHits;if(misses)*misses=poolMisses;
  return;
 }
#ifdef OPT_ROM_CACHE
 if(calls)*calls=st_readCalls;if(hits)*hits=st_blockHit;if(misses)*misses=st_blockMiss;
#else
 if(calls)*calls=0;if(hits)*hits=0;if(misses)*misses=0;
#endif
}

/* The handle pool's own numbers, for the report in memlog.c. */
void VitaNativeRomFSPoolStats(unsigned*evictions,unsigned*openFail,unsigned*handles){
 if(evictions)*evictions=poolEvictions;
 if(openFail)*openFail=poolOpenFail;
 if(handles){unsigned n=0;for(int i=0;i<POOL_HANDLES;i++)if(pool[i].fp)n++;*handles=n;}
}
