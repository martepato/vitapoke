#include <nitro.h>
#include <simulator/drawmsg.h>
#include <simulator/g3_handler.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
/* The DS sends a packed list of geometry commands to its graphics FIFO by DMA. This port has no DMA
 * engine and no FIFO: the geometry simulator reads the list where it already is, synchronously, so
 * the transfer is complete the moment the dispatch returns and the asynchronous forms below can call
 * their callbacks immediately. The list itself is not copied -- the caller's buffer is still valid
 * for the duration of the call, which is the DS's own contract for it. */
void MI_SendGXCommand(u32 dma,const void*src,u32 bytes){
 draw_msg_t msg;
 if(dma>3||(bytes&3)||((uintptr_t)src&3)||(!src&&bytes))abort();if(!bytes)return;
 memset(&msg,0,sizeof msg);
 msg.type=DRAW_CMD_G3_CMD_LIST;msg.size=bytes;msg.data.ptr=(void*)src;
 SIM_HandleG3Command(&msg);
}
void MI_SendGXCommandFast(u32 dma,const void*src,u32 bytes){MI_SendGXCommand(dma,src,bytes);}
void MI_SendGXCommandAsync(u32 dma,const void*src,u32 bytes,MIDmaCallback cb,void*arg){MI_SendGXCommand(dma,src,bytes);if(cb)cb(arg);}
void MI_SendGXCommandAsyncFast(u32 dma,const void*src,u32 bytes,MIDmaCallback cb,void*arg){MI_SendGXCommandAsync(dma,src,bytes,cb,arg);}
