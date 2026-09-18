/* vitapoke: the port's log, its failure path, and what it reports about memory.
 *
 * printf goes nowhere on a console, so anything worth knowing after boot goes to a file on the
 * memory card, next to the ROM and the save. That file is the only diagnostic a user can send back,
 * so a line in it has to be worth reading: this logs events (startup, scene transitions, a new
 * allocation high-water mark), never frames.
 *
 * The Vita's numbers are not the PSP's. The PSP build measured the 24 MiB user partition it had to
 * fit the whole game into and reported how close it was; the Vita gives an application 512 MiB and
 * the interesting number is no longer "how much is left" but "how much did the port take", so that
 * is what this reports.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "include/vita_os.h"
#include "include/vitapoke.h"

static int logStarted;

void VitaNativeMemLog(const char *fmt, ...)
{
	char line[400];
	va_list ap;
	SceUID fd;

	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	printf("%s\n", line);   /* visible under an emulator and over a debug link; nowhere else */

	sceIoMkdir(VITAPOKE_DATA_DIR, 0777);
	fd = sceIoOpen(VITAPOKE_LOG_PATH,
	               logStarted ? (SCE_O_WRONLY | SCE_O_APPEND) : (SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC),
	               0777);
	if (fd >= 0) {
		logStarted = 1;
		sceIoWrite(fd, line, strlen(line));
		sceIoWrite(fd, "\n", 1);
		sceIoClose(fd);
	}
}

/* The end of the road. Every impossible case in the port comes here rather than to abort(), so that
 * the reason reaches the log before the application goes away: a Vita that drops back to the LiveArea
 * tells the user nothing at all. */
void VitaNativeFatal(const char *message)
{
	VitaNativeMemLog("[FATAL] %s", message ? message : "(no message)");
	sceKernelExitProcess(1);
	for (;;)
		sceKernelDelayThread(1000000);   /* unreachable; keeps the compiler from falling through */
}

/* The C library's abort, replaced.
 *
 * newlib's abort raises SIGABRT, and psp2 has no signals: the raise lands in a kill stub that is not
 * implemented, and the console dies on an undefined instruction with nothing written down. That is
 * the worst possible failure for this port, because abort is where the DS SDK's assertions and a
 * dozen of the port's own impossible cases end up. Replacing it costs nothing -- the linker takes
 * this definition and never pulls newlib's -- and turns every one of those into a line in the log.
 *
 * Anything in the port that has something to say should still call VitaNativeFatal with a message.
 * This is for the calls that cannot: inside the C library, inside the decompilation, inside the SDK.
 */
void abort(void)
{
	char message[96];

	/* The caller's address, so that whoever reads the log can find out who gave up: run
	 * arm-vita-eabi-nm -n on the .elf in the build tree and look for the symbol below it. */
	snprintf(message, sizeof message, "abort() called from %p", __builtin_return_address(0));
	VitaNativeFatal(message);
	for (;;)
		;
}

/* ---------------------------------------------------------------- the main thread's stack
 *
 * The whole DS game runs on one thread, and DS code nests deeply. An overflow would show up as a
 * crash with no explanation, so the unused part of the stack is filled with a pattern at startup and
 * this reports how far the game has since written into it.
 *
 * psp2 will answer the same question directly with sceKernelGetThreadStackFreeSize, but only for the
 * calling thread and only as "free now", not "least free ever". The pattern is what gives the high
 * water mark.
 */
#define STACK_PATTERN 0xCDCDCDCDu

static unsigned *fillLow, *fillHigh;
static unsigned stackTop, stackBytes;

void VitaNativeStackProbeInit(void)
{
	SceKernelThreadInfo info;
	unsigned sp, low, high;

	memset(&info, 0, sizeof info);
	info.size = sizeof info;
	if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &info) < 0) {
		VitaNativeMemLog("[STACK] thread info unavailable; no stack measurement");
		return;
	}
	__asm__ volatile("mov %0, sp" : "=r"(sp));
	stackTop = (unsigned)(uintptr_t)info.stack + (unsigned)info.stackSize;
	stackBytes = (unsigned)info.stackSize;
	VitaNativeMemLog("[STACK] stack=%p size=%u sp=%08x", info.stack, stackBytes, sp);

	/* Leave a generous gap below the live frame so the fill never scribbles on this function. */
	low = ((unsigned)(uintptr_t)info.stack + 1024 + 15) & ~15u;
	high = (sp - 8192) & ~15u;
	if (high <= low || high > stackTop) {
		VitaNativeMemLog("[STACK] range rejected low=%08x high=%08x top=%08x", low, high, stackTop);
		return;
	}
	fillLow = (unsigned *)(uintptr_t)low;
	fillHigh = (unsigned *)(uintptr_t)high;
	for (unsigned *p = fillLow; p < fillHigh; p++)
		*p = STACK_PATTERN;
}

/* Bytes of stack ever used, and the total; 0 if unmeasured. */
unsigned VitaNativeStackHighWater(unsigned *total)
{
	unsigned *p = fillLow;

	if (total)
		*total = stackBytes;
	if (!fillLow)
		return 0;
	while (p < fillHigh && *p == STACK_PATTERN)
		p++;
	return stackTop - (unsigned)(uintptr_t)p;
}

/* ---------------------------------------------------------------- memory reports
 *
 * Weak, because the renderer and the file system are separate pieces of the port and a test binary
 * may link only some of them.
 */
extern void VitaNativeRenderMemStats(unsigned *bytes, unsigned *high, unsigned *blocks) __attribute__((weak));
extern void VitaNativeRomFSStats(unsigned *open, unsigned *high, unsigned *openFail, unsigned *readFail) __attribute__((weak));
extern void VitaNativeRomFSPoolStats(unsigned *evictions, unsigned *openFail, unsigned *handles) __attribute__((weak));
extern unsigned VitaNativeRenderFrameCount(void) __attribute__((weak));

static unsigned heapHigh;

void VitaNativeMemReport(const char *tag)
{
	struct mallinfo mi = mallinfo();
	unsigned gpuBytes = 0, gpuHigh = 0, gpuBlocks = 0;
	unsigned fsOpen = 0, fsHigh = 0, fsOpenFail = 0, fsReadFail = 0;
	unsigned poolHandles = 0, poolEvict = 0, poolFail = 0;
	unsigned stackTotal = 0, stackUsed = VitaNativeStackHighWater(&stackTotal);

	if (VitaNativeRenderMemStats)
		VitaNativeRenderMemStats(&gpuBytes, &gpuHigh, &gpuBlocks);
	if (VitaNativeRomFSStats)
		VitaNativeRomFSStats(&fsOpen, &fsHigh, &fsOpenFail, &fsReadFail);
	if (VitaNativeRomFSPoolStats)
		VitaNativeRomFSPoolStats(&poolEvict, &poolFail, &poolHandles);
	if ((unsigned)mi.uordblks > heapHigh)
		heapHigh = mi.uordblks;

	VitaNativeMemLog("[MEM] %s frame=%u stack=%u/%u heap_used=%d heap_high=%u heap_arena=%d "
	                "gpu=%u B (high %u B, %u blocks) files_open=%u/%u open_fail=%u read_fail=%u "
	                /* The handle pool: how many host files are open of the bound, how often one had
	                 * to be closed to make room, and how often one would not open. All zero on a
	                 * build that reads a ROM instead, which has one handle by design. */
	                "handles=%u evicted=%u handle_fail=%u",
	                tag ? tag : "", VitaNativeRenderFrameCount ? VitaNativeRenderFrameCount() : 0,
	                stackUsed, stackTotal, mi.uordblks, heapHigh, mi.arena,
	                gpuBytes, gpuHigh, gpuBlocks, fsOpen, fsHigh, fsOpenFail, fsReadFail,
	                poolHandles, poolEvict, poolFail);
}

/* Called every few frames. Reports only when the heap reaches a new high, so a steady frame costs a
 * mallinfo call and nothing else. */
void VitaNativeMemPoll(void)
{
	struct mallinfo mi = mallinfo();

	if ((unsigned)mi.uordblks > heapHigh + 64 * 1024) {
		heapHigh = mi.uordblks;
		VitaNativeMemReport("heap high water");
	}
}
