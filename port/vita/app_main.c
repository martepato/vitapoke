/* vitapoke: the application's entry point on the Vita.
 *
 * Everything before NitroMain: find the game's files, bring up the log, the save and the frame
 * driver, and then hand control to the DS game's own main, which never returns.
 *
 * The one structural difference from the PSP build is the thread. There, the whole DS game ran on
 * the process's main thread, whose stack size a PSPSDK macro could set. psp2 has no such hook -- the
 * main thread's stack is what the module was built with -- and the DS game nests deeply enough that
 * 256 KB is not obviously safe. So main() creates the game's thread with an explicit 1 MiB stack and
 * waits for it. That also keeps the DS execution lock honest: the thread that runs DS code is a
 * thread this port made, like every other DS thread.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nitro.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

#include "include/vita_os.h"
#include "include/vitapoke.h"

/* The malloc heap. The game's own allocator lives in the 6 MiB DS arena in os_core.c and never
 * touches this; what does is the renderer (decoded 3D textures, up to 2 MiB) and the C library. The
 * PSP build had to fit everything into a 24 MiB partition and measured this to the kilobyte; the
 * Vita gives an application 512 MiB, so this is generous on purpose and still a rounding error. */
unsigned int _newlib_heap_size_user = 64 * 1024 * 1024;

/* Padding, placed by overlays/gen-link.py's linker script fragment so that the read-only segment
 * ends 256 bytes into a page. vita-elf-create needs the rest of that page for the module's SCE
 * metadata; the note in gen-link.py has the whole story. It is here rather than in a file of its
 * own because it belongs to the module rather than to any part of the port. */
static const unsigned char scePadding[0x100] __attribute__((used, section(".scepad"))) = { 0 };

extern void NitroMain(void);
extern void VitaNativeStackProbeInit(void);
extern void VitaNativeFrameInit(void);
extern BOOL VitaNativeOverlay_Init(void);
extern BOOL VitaNativeRomFS_SetPath(const char *path);
extern void FS_Init(u32 channel);
extern BOOL FS_IsAvailable(void);
extern BOOL VitaNative_OpenBackup(const char *path);

/* The SDK's texture and palette base tables are zero until these run. On the DS they are filled by
 * the hardware's own initialisation; in libntr's portable build the host program calls them, so the
 * port has to. */
extern void WIN_Init_sTexStartAddrTable(void);
extern void WIN_Init_sTexPlttStartAddrTable(void);

static int Exists(const char *path)
{
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);

	if (fd < 0)
		return 0;
	sceIoClose(fd);
	return 1;
}

static int GameThread(SceSize args, void *argp)
{
	(void)args;
	(void)argp;

	VitaNativeStackProbeInit();

	/* Where the game's data comes from. A build made with a ROM has it unpacked into the
	 * application itself -- 340 files and an index beside the executable -- and needs nothing on
	 * the memory card. A build made without one reads the ROM from the card, as this port did
	 * before the assets could be bundled. The file system layer picks between them; all that is
	 * needed here is to point it at the card when there is nothing bundled, and to say clearly
	 * when there is neither. */
	if (!Exists(VITAPOKE_ASSET_INDEX)) {
		if (!Exists(VITAPOKE_ROM_PATH)) {
			VitaNativeMemLog("[APP] this build has no game data in it and there is no ROM at %s.",
			                 VITAPOKE_ROM_PATH);
			VitaNativeMemLog("[APP] Either copy your own Platinum dump there, or build with"
			                 " --rom so the data is packed into the VPK.");
			sceKernelExitProcess(1);
		}
		if (!VitaNativeRomFS_SetPath(VITAPOKE_ROM_PATH)) {
			VitaNativeMemLog("[APP] %s could not be opened as a DS ROM", VITAPOKE_ROM_PATH);
			sceKernelExitProcess(1);
		}
	}
	FS_Init(0);
	if (!FS_IsAvailable()) {
		VitaNativeMemLog("[APP] the game's file system did not start; see the lines above");
		sceKernelExitProcess(1);
	}
	/* The overlay table comes from whichever of the two the file system is serving, which is also
	 * how data that is not this game, or not this region, is caught: its table has a different
	 * number of modules. */
	if (!VitaNativeOverlay_Init()) {
		VitaNativeMemLog("[APP] the overlay table could not be read; see the lines above");
		sceKernelExitProcess(1);
	}
	/* A missing save is created, blank, as a new cartridge would be. A save that is there but is
	 * not 512 KB is left alone: the game writes its own layout over the whole file, so anything
	 * else at that path is somebody else's data and this port cannot put it back. The line above
	 * this one in the log says which of those happened. */
	if (!VitaNative_OpenBackup(VITAPOKE_SAVE_PATH)) {
		VitaNativeMemLog("[APP] no usable save at %s: it must be exactly 512 KB, and what is there "
		                 "is not, so it has been left untouched", VITAPOKE_SAVE_PATH);
		sceKernelExitProcess(1);
	}

	WIN_Init_sTexStartAddrTable();
	WIN_Init_sTexPlttStartAddrTable();
	/* The Vita has no GBA slot. Letting the real SDK run its absent-cartridge path is what makes
	 * the game's dual-slot checks answer "nothing there" rather than never being asked. */
	CTRDG_Init();
	VitaNativeFrameInit();

	VitaNativeMemLog("[APP] entering NitroMain");
	NitroMain();
	/* NitroMain does not return on a DS and does not here either; if it ever does, say so. */
	VitaNativeMemLog("[APP] NitroMain returned");
	sceKernelExitProcess(0);
	return 0;
}

int main(void)
{
	SceUID game;

	(void)scePadding;

	sceIoMkdir(VITAPOKE_DATA_DIR, 0777);
	VitaNativeMemLog("[APP] vitapoke starting: data in %s", VITAPOKE_DATA_DIR);

	/* The DS game is a fixed 60 Hz design doing software compositing for two screens; run the
	 * console at its full clock rather than whatever the shell left it at. */
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);

	/* The game's own thread, and it is a DS thread: port/vita/os_thread.c registers it as the DS's
	 * main thread at DS priority 16, which is where the DS runs it, and maps DS priorities to
	 * psp2's by adding 0x60. Starting it anywhere else would make every thread the game creates
	 * lower or higher priority than it expects. 1 MiB of stack: see the note at the top. */
	game = sceKernelCreateThread("vitapoke_game", GameThread, 0x60 + 16, 1024 * 1024, 0, 0, NULL);
	if (game < 0) {
		VitaNativeMemLog("[APP] could not create the game thread: %08x", (int)game);
		return 1;
	}
	sceKernelStartThread(game, 0, NULL);
	/* After the game thread, so a watchdog report can never be the first thing in the log. */
	VitaNativeWatchdogStart();
	sceKernelWaitThreadEnd(game, NULL, NULL);
	return 0;
}
