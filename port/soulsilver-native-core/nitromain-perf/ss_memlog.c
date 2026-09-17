/* Card log for the SoulSilver native port (playbook item 2).
 * printf is invisible on a real PSP. Every line that matters for a post-mortem
 * is mirrored to native-memlog.txt next to the EBOOT, opened/appended/closed
 * per line so it survives a crash. Off unless VITAPOKE_MEMLOG=1. */
#include <pspkernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifndef VITAPOKE_MEMLOG
#define VITAPOKE_MEMLOG 0
#endif
static int started;
void VitaNativeMemLog(const char *fmt, ...){
 char line[400]; va_list ap; va_start(ap,fmt); vsnprintf(line,sizeof line,fmt,ap); va_end(ap);
 printf("%s\n",line);
#if VITAPOKE_MEMLOG
 SceUID fd=sceIoOpen("native-memlog.txt",started?(PSP_O_WRONLY|PSP_O_APPEND):(PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC),0777);
 if(fd>=0){started=1;sceIoWrite(fd,line,strlen(line));sceIoWrite(fd,"\n",1);sceIoClose(fd);}
#endif
}
