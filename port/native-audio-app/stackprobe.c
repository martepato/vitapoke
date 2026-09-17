/* Main-thread stack high-water measurement.
   Fills the unused part of the stack with a pattern at startup, then reports how far
   the game has since written down into it. The pspsdk default main stack is 256 KB and
   the port runs the whole DS game on it, so overflow is a real candidate for a crash
   that returns to the XMB with no diagnostic. */
#include <pspkernel.h>
#include <pspthreadman.h>
#include <string.h>
#include <stdio.h>
extern void PSPNativeMemLog(const char*,...);

#define STACK_PATTERN 0xCDCDCDCDu

static unsigned *fillLow, *fillHigh;      /* [low, high) filled with the pattern */
static unsigned stackBase, stackSize;

void PSPNativeStackProbeInit(void)
{
    SceKernelThreadInfo info;
    memset(&info, 0, sizeof info);
    info.size = sizeof info;
    int rc = sceKernelReferThreadStatus(sceKernelGetThreadId(), &info);
    unsigned sp0;
#if defined(__arm__)
    asm volatile("mov %0, sp" : "=r"(sp0));
#else
    asm volatile("move %0, $sp" : "=r"(sp0));
#endif
    PSPNativeMemLog("[STACK] refer rc=%d stack=%p size=%u sp=%08x", rc, info.stack, (unsigned)info.stackSize, sp0);
    if (rc < 0) return;
    stackBase = (unsigned)(uintptr_t)info.stack;
    stackSize = (unsigned)info.stackSize;

    unsigned sp = sp0;
    /* leave a generous gap below the live frame so we never scribble on ourselves */
    unsigned low  = (stackBase + 1024 + 15) & ~15u;
    unsigned high = (sp - 8192) & ~15u;
    if (high <= low || high > stackBase + stackSize) {
        PSPNativeMemLog("[STACK] range rejected base=%08x size=%u low=%08x high=%08x", stackBase, stackSize, low, high);
        return;
    }
    fillLow = (unsigned *)(uintptr_t)low;
    fillHigh = (unsigned *)(uintptr_t)high;
    for (unsigned *p = fillLow; p < fillHigh; p++) *p = STACK_PATTERN;
}

/* returns bytes of stack ever used, and the total size, 0 if unmeasured */
unsigned PSPNativeStackHighWater(unsigned *total)
{
    if (total) *total = stackSize;
    if (!fillLow) return 0;
    unsigned *p = fillLow;
    while (p < fillHigh && *p == STACK_PATTERN) p++;
    unsigned deepest = (unsigned)(uintptr_t)p;          /* lowest address still untouched */
    return stackBase + stackSize - deepest;
}
