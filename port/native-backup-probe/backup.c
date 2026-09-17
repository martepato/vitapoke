#include <nitro.h>
#include <pspkernel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BACKUP_BYTES (512u*1024u)
static FILE *file;
static SceUID mutex=-1;
static int owner=-1;
static CARDResult result=CARD_RESULT_NO_RESPONSE;
BOOL VitaNative_OpenBackup(const char *path){
 if(file||owner!=-1)return FALSE;
 FILE *candidate=fopen(path,"r+b");if(!candidate)return FALSE;
 if(fseek(candidate,0,SEEK_END)||ftell(candidate)!=BACKUP_BYTES){fclose(candidate);return FALSE;}
 mutex=sceKernelCreateSema("NativeBackup",0,1,1,NULL);if(mutex<0){fclose(candidate);return FALSE;}
 file=candidate;result=CARD_RESULT_SUCCESS;return TRUE;
}
BOOL VitaNative_CloseBackup(void){
 if(owner!=-1||!file)return FALSE;
 int status=fclose(file);file=NULL;sceKernelDeleteSema(mutex);mutex=-1;
 result=status?CARD_RESULT_FAILURE:CARD_RESULT_SUCCESS;return status==0;
}
void CARD_LockBackup(u16 id){if(mutex<0||sceKernelWaitSema(mutex,1,NULL)<0)abort();owner=id;}
void CARD_UnlockBackup(u16 id){if(owner!=id)abort();owner=-1;if(sceKernelSignalSema(mutex,1)<0)abort();}
BOOL CARD_IdentifyBackup(CARDBackupType type){result=file&&type==CARD_BACKUP_TYPE_FLASH_4MBITS?CARD_RESULT_SUCCESS:CARD_RESULT_UNSUPPORTED;return result==CARD_RESULT_SUCCESS;}
u32 CARD_GetBackupTotalSize(void){return BACKUP_BYTES;}
u32 CARD_GetBackupSectorSize(void){return 65536;}
u32 CARD_GetBackupPageSize(void){return 256;}
CARDResult CARD_GetResultCode(void){return result;}
BOOL CARD_TryWaitBackupAsync(void){return TRUE;}
BOOL CARD_WaitBackupAsync(void){return result==CARD_RESULT_SUCCESS;}
void CARD_CancelBackupAsync(void){} /* Synchronous work has already completed. */
static BOOL Verify(u32 offset,const u8 *source,u32 length){
 u8 buf[256];if(fseek(file,offset,SEEK_SET))return FALSE;
 while(length){u32 amount=length>sizeof(buf)?sizeof(buf):length;if(fread(buf,1,amount,file)!=amount||memcmp(buf,source,amount))return FALSE;source+=amount;length-=amount;}
 return TRUE;
}
BOOL CARDi_RequestStreamCommand(u32 src,u32 dst,u32 length,MIDmaCallback callback,void *arg,BOOL async,CARDRequest request,int retry,CARDRequestMode mode){
 (void)async;(void)retry;
 u32 offset=request==CARD_REQ_READ_BACKUP?src:dst;
 result=CARD_RESULT_SUCCESS;
 if(!file||owner<0)result=CARD_RESULT_NO_RESPONSE;
 else if(offset>BACKUP_BYTES||length>BACKUP_BYTES-offset)result=CARD_RESULT_INVALID_PARAM;
 else if(request==CARD_REQ_READ_BACKUP){
  if(mode!=CARD_REQUEST_MODE_RECV||(!dst&&length))result=CARD_RESULT_INVALID_PARAM;
  else if(fseek(file,offset,SEEK_SET)||fread((void*)(uintptr_t)dst,1,length,file)!=length)result=CARD_RESULT_FAILURE;
 }else if(request==CARD_REQ_WRITE_BACKUP){
  if((mode!=CARD_REQUEST_MODE_SEND&&mode!=CARD_REQUEST_MODE_SEND_VERIFY)||(!src&&length))result=CARD_RESULT_INVALID_PARAM;
  else if(fseek(file,offset,SEEK_SET)||fwrite((const void*)(uintptr_t)src,1,length,file)!=length||fflush(file))result=CARD_RESULT_FAILURE;
  else if(mode==CARD_REQUEST_MODE_SEND_VERIFY&&!Verify(offset,(const u8*)(uintptr_t)src,length))result=CARD_RESULT_FAILURE;
 }else if(request==CARD_REQ_VERIFY_BACKUP){
  if(mode!=CARD_REQUEST_MODE_SEND||(!src&&length))result=CARD_RESULT_INVALID_PARAM;
  else if(!Verify(offset,(const u8*)(uintptr_t)src,length))result=CARD_RESULT_FAILURE;
 }else result=CARD_RESULT_UNSUPPORTED;
 BOOL ok=result==CARD_RESULT_SUCCESS;if(callback)callback(arg);return ok;
}
