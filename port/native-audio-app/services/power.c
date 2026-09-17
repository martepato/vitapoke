#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
/* There is no DS lid to close here; the SDK's callback registrations are kept as they are.
   Entering DS
   sleep/power-off is unsupported and stops explicitly instead of succeeding. */
static PMSleepCallbackInfo *before,*after;
static BOOL ready;
void PM_Init(void){if(!ready){before=after=NULL;ready=TRUE;}}
static void Insert(PMSleepCallbackInfo**list,PMSleepCallbackInfo*info,BOOL append){if(!info||!info->callback)abort();for(PMSleepCallbackInfo*p=*list;p;p=p->next)if(p==info)abort();if(append){while(*list)list=&(*list)->next;info->next=NULL;*list=info;}else{info->next=*list;*list=info;}}
static void Delete(PMSleepCallbackInfo**list,PMSleepCallbackInfo*info){while(*list&&*list!=info)list=&(*list)->next;if(*list){*list=info->next;info->next=NULL;}}
void PM_PrependPreSleepCallback(PMSleepCallbackInfo*i){Insert(&before,i,FALSE);}
void PM_AppendPostSleepCallback(PMSleepCallbackInfo*i){Insert(&after,i,TRUE);}
void PM_DeletePreSleepCallback(PMSleepCallbackInfo*i){Delete(&before,i);}
void PM_DeletePostSleepCallback(PMSleepCallbackInfo*i){Delete(&after,i);}
void PM_GoSleepMode(PMWakeUpTrigger trigger,PMLogic logic,u16 keys){(void)trigger;(void)logic;(void)keys;puts("[NATIVE] DS sleep mode is not supported on this console");abort();}
u32 PM_ForceToPowerOff(void){puts("[NATIVE] DS power-off is not supported on this console");abort();}
