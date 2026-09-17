#include <nitro.h>
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
PSP_MODULE_INFO("Native backup probe",0,1,0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU);
extern BOOL VitaNative_OpenBackup(const char*);extern BOOL VitaNative_CloseBackup(void);
static unsigned callbacks;static CARDResult callbackResult;
static void Done(void*arg){callbacks+=*(unsigned*)arg;callbackResult=CARD_GetResultCode();}
int main(void){unsigned failures=0,one=1;u8 input[777],output[777];for(unsigned i=0;i<777;i++)input[i]=(i*37u+11u)&255;
if(!VitaNative_OpenBackup("probe-save.bin")){printf("[BACKUP] open failed\n");return 1;}
CARD_LockBackup(12);if(!CARD_IdentifyBackup(CARD_BACKUP_TYPE_FLASH_4MBITS)||CARD_GetBackupTotalSize()!=524288)failures++;
if(!CARD_ReadFlash(0,output,777))failures++;for(unsigned i=0;i<777;i++)if(output[i]!=255)failures++;
CARD_WriteAndVerifyFlashAsync(255,input,777,Done,&one);
if(callbacks!=1||callbackResult!=CARD_RESULT_SUCCESS||!CARD_TryWaitBackupAsync()||!CARD_WaitBackupAsync())failures++;
if(!CARD_ReadFlash(255,output,777)||memcmp(input,output,777)||!CARD_WriteAndVerifyFlash(255,input,777))failures++;
input[0]^=1;if(CARD_VerifyFlash(255,input,777)||CARD_GetResultCode()!=CARD_RESULT_FAILURE)failures++;input[0]^=1;
CARD_ReadFlashAsync(524280,output,777,Done,&one);if(callbacks!=2||callbackResult!=CARD_RESULT_INVALID_PARAM||CARD_WaitBackupAsync())failures++;
CARD_WriteAndVerifyFlashAsync(524280,input,777,Done,&one);if(callbacks!=3||callbackResult!=CARD_RESULT_INVALID_PARAM)failures++;
if(CARD_ProgramBackup(255,input,777)||CARD_GetResultCode()!=CARD_RESULT_UNSUPPORTED)failures++;
if(!CARD_ReadFlash(254,output,1)||output[0]!=255||!CARD_ReadFlash(1032,output,1)||output[0]!=255)failures++;
CARD_UnlockBackup(12);if(!VitaNative_CloseBackup()||!VitaNative_OpenBackup("probe-save.bin"))failures++;
CARD_LockBackup(12);if(!CARD_ReadFlash(255,output,777)||memcmp(input,output,777))failures++;CARD_UnlockBackup(12);if(!VitaNative_CloseBackup())failures++;
CARD_ReadFlashAsync(0,output,1,Done,&one);if(callbacks!=4||callbackResult!=CARD_RESULT_NO_RESPONSE)failures++;
if(VitaNative_OpenBackup("does-not-exist.bin"))failures++;
printf("[BACKUP] failures=%u callbacks=%u persistent777=checked\n",failures,callbacks);sceKernelExitGame();return failures;}
