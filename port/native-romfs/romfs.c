// Read-only native PSP NitroFS backend. All ROM handles are opened "rb".
#include <nitro/fs.h>
#include <nitro/card.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char romPath[512]="Platinum.nds";
static FSArchive archive;
static unsigned char header[512] __attribute__((aligned(4)));
static unsigned char *tables,*fat,*fnt;
static u32 romSize,fatSize,fntSize,dirCount,currentDir,dma;
static BOOL ready;
static u16 U16(const void*p){const u8*b=p;return b[0]|((u16)b[1]<<8);}
static u32 U32(const void*p){const u8*b=p;return U16(b)|((u32)U16(b+2)<<16);}
BOOL VitaNativeRomFS_SetPath(const char*path){if(ready||!path||strlen(path)>=sizeof(romPath))return FALSE;strcpy(romPath,path);return TRUE;}
void FS_End(void){free(tables);tables=fat=fnt=NULL;ready=FALSE;memset(&archive,0,sizeof(archive));}
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
 fclose(stream);dirCount=U16(fnt+6);
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
 FILE*s=fopen(romPath,"rb");if(!s)return FALSE;if(fseek(s,top,SEEK_SET)){fclose(s);return FALSE;}
 f->pcFilePtr=s;f->arc=a;f->stat=FS_FILE_STATUS_IS_FILE;f->error=FS_RESULT_SUCCESS;
 f->prop.file.own_id=index;f->prop.file.top=top;f->prop.file.bottom=bottom;f->prop.file.pos=top;return TRUE;
}
BOOL FS_OpenFileFast(FSFile*f,FSFileID id){if(!ready||!f||id.arc!=&archive||id.file_id>=fatSize/8)return FALSE;const u8*p=fat+id.file_id*8;return FS_OpenFileDirect(f,&archive,U32(p),U32(p+4),id.file_id);}
BOOL FS_OpenFile(FSFile*f,const char*path){FSFileID id;return FS_ConvertPathToFileID(&id,path)&&FS_OpenFileFast(f,id);}
BOOL FS_CloseFile(FSFile*f){if(!f)return FALSE;BOOL ok=TRUE;if(f->pcFilePtr)ok=fclose(f->pcFilePtr)==0;f->pcFilePtr=NULL;f->stat=0;f->arc=NULL;return ok;}
s32 FS_ReadFile(FSFile*f,void*dst,s32 len){
 if(!f||!f->pcFilePtr||!dst||len<0)return -1;
 u32 rest=f->prop.file.bottom-f->prop.file.pos;if((u32)len>rest)len=rest;
 size_t n=fread(dst,1,len,f->pcFilePtr);f->prop.file.pos+=n;
 if(ferror(f->pcFilePtr)){f->error=FS_RESULT_FAILURE;return -1;}f->error=FS_RESULT_SUCCESS;return n;
}
s32 FS_ReadFileAsync(FSFile*f,void*dst,s32 len){return FS_ReadFile(f,dst,len);}
BOOL FS_WaitAsync(FSFile*f){return f&&f->error==FS_RESULT_SUCCESS;}
s32 FS_WriteFile(FSFile*f,const void*src,s32 len){(void)src;(void)len;if(f)f->error=FS_RESULT_UNSUPPORTED;return -1;}
s32 FS_WriteFileAsync(FSFile*f,const void*src,s32 len){return FS_WriteFile(f,src,len);}
BOOL FS_SeekFile(FSFile*f,s32 offset,FSSeekFileMode mode){
 if(!f||!f->pcFilePtr)return FALSE;int64_t pos=offset;
 if(mode==FS_SEEK_SET)pos+=f->prop.file.top;else if(mode==FS_SEEK_CUR)pos+=f->prop.file.pos;else if(mode==FS_SEEK_END)pos+=f->prop.file.bottom;else return FALSE;
 if(pos<f->prop.file.top)pos=f->prop.file.top;if(pos>f->prop.file.bottom)pos=f->prop.file.bottom;
 if(fseek(f->pcFilePtr,(long)pos,SEEK_SET))return FALSE;f->prop.file.pos=pos;return TRUE;
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
