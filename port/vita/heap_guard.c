/* vitapoke: a guarded malloc, for finding the write that corrupts the heap.
 *
 * The port crashed inside newlib's _malloc_r, walking its bins with a null chunk pointer, about
 * thirty seconds into the game's opening. A crash there says only that somebody wrote outside a
 * block; it never says who, because by the time malloc trips over the damage the code that did it is
 * long gone. This file closes that gap.
 *
 * It is not in a normal build. `make MALLOC_GUARD=1` compiles it with VITAPOKE_HEAP_GUARD and links
 * with --wrap on the four allocators, so every allocation in the application -- the port's, the SDK
 * shims', vitaGL's -- goes through here:
 *
 *   - each block gets a header (a magic word, its size, and the address that asked for it) and a
 *     trailer magic word just past the payload, so an overflow of even one byte is visible;
 *   - free checks both, and reports who allocated the block and who is freeing it;
 *   - every SWEEP_EVERY calls, every live block is checked, so an overflow is caught within a few
 *     hundred allocations of being made rather than whenever malloc next happens to trip;
 *   - a free of a pointer this file did not hand out, or of one already freed, is reported instead
 *     of being passed on to newlib, which would corrupt the heap for real.
 *
 * The cost is 32 bytes and a list walk per allocation, which is why it is a switch and not the
 * default. Everything it finds is reported through VitaNativeMemLog and then made fatal, because a
 * corrupted heap makes every later report a guess.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <psp2/kernel/threadmgr.h>

#include "include/vita_os.h"

/* The DS's main RAM, as the backing generator defines it: a pointer inside this is the game's own
 * allocator's, never libc's. */
extern unsigned char s_HW_MAIN_MEM[0x800000];

void *__real_malloc(size_t size);
void __real_free(void *p);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *p, size_t size);
void *__real_memalign(size_t alignment, size_t size);

#define HEAD_MAGIC 0x5645434bu   /* "VECK" */
#define TAIL_MAGIC 0x4b434556u
#define DEAD_MAGIC 0xdeadbeefu
#define SWEEP_EVERY 256

struct Block {
	unsigned magic;
	size_t size;
	const void *caller;
	void *base;            /* what to hand __real_free: the header for malloc, earlier for memalign */
	struct Block *prev, *next;
};

static struct Block *live;
static unsigned operations, blocks;
static SceKernelLwMutexWork guardLock;
static int lockReady;

static void Lock(void)
{
	if (!lockReady) {
		sceKernelCreateLwMutex(&guardLock, "vitapoke_heap_guard", SCE_KERNEL_MUTEX_ATTR_RECURSIVE,
		                       0, NULL);
		lockReady = 1;
	}
	sceKernelLockLwMutex(&guardLock, 1, NULL);
}

static void Unlock(void) { sceKernelUnlockLwMutex(&guardLock, 1); }

static unsigned char *Payload(struct Block *b) { return (unsigned char *)(b + 1); }

static unsigned *Tail(struct Block *b)
{
	/* The payload is rounded up to a word so the trailer is aligned. */
	return (unsigned *)(Payload(b) + ((b->size + 3u) & ~3u));
}

/* Returns a description of what is wrong with a block, or NULL when it is intact. */
static const char *Check(struct Block *b)
{
	if (b->magic == DEAD_MAGIC)
		return "freed twice";
	if (b->magic != HEAD_MAGIC)
		return "header overwritten (the block before it overflowed)";
	if (*Tail(b) != TAIL_MAGIC)
		return "overflowed past its end";
	return NULL;
}

static void Fail(struct Block *b, const char *what, const void *by)
{
	char message[192];

	snprintf(message, sizeof message,
	         "[HEAP] %p (%u bytes, allocated from %p) %s; reported by %p",
	         (void *)Payload(b), (unsigned)b->size, b->caller, what, by);
	VitaNativeMemLog("%s", message);
	VitaNativeFatal(message);
}

/* Walk every live block. Called every SWEEP_EVERY operations, with the lock held. */
static void Sweep(const void *by)
{
	for (struct Block *b = live; b; b = b->next) {
		const char *what = Check(b);
		if (what)
			Fail(b, what, by);
	}
}

static void *Allocate(size_t size, const void *caller)
{
	struct Block *b;
	size_t rounded = (size + 3u) & ~3u;

	b = (struct Block *)__real_malloc(sizeof *b + rounded + sizeof(unsigned));
	if (!b)
		return NULL;
	b->magic = HEAD_MAGIC;
	b->size = size;
	b->caller = caller;
	b->base = b;
	*Tail(b) = TAIL_MAGIC;

	Lock();
	b->prev = NULL;
	b->next = live;
	if (live)
		live->prev = b;
	live = b;
	blocks++;
	if (++operations % SWEEP_EVERY == 0)
		Sweep(caller);
	Unlock();
	return Payload(b);
}

static void Release(void *p, const void *caller)
{
	struct Block *b;
	const char *what;

	if (!p)
		return;
	b = (struct Block *)p - 1;
	/* A pointer with neither magic word did not come from here, and there are two very different
	 * reasons for that.
	 *
	 * The harmless one: newlib allocates a few things internally through _malloc_r, which this wrap
	 * does not see, and something later frees one through free(). Handing it to the real free is
	 * exactly right.
	 *
	 * The one worth hunting: a pointer into the DS's own memory. The game allocates from its arena
	 * inside s_HW_MAIN_MEM, and a shim that passes one of those to libc's free sends the allocator
	 * walking a "chunk header" made of whatever DS data happens to sit in front of it. That is
	 * fatal, it is fatal later and somewhere else, and it is the exact bug this file was written to
	 * find -- so name the caller and do not pass it on.
	 *
	 * Either way the first few are logged with the address and the caller, because one global
	 * "seen it" flag would let the interesting case hide behind the harmless one. */
	if (b->magic != HEAD_MAGIC && b->magic != DEAD_MAGIC) {
		static unsigned foreign;
		int dsMemory = (unsigned char *)p >= s_HW_MAIN_MEM &&
		               (unsigned char *)p < s_HW_MAIN_MEM + sizeof s_HW_MAIN_MEM;

		if (++foreign <= 8)
			VitaNativeMemLog("[HEAP] free(%p) from %p: %s", p, caller,
			                 dsMemory ? "a pointer into the DS's own memory, not from libc -- "
			                            "NOT freeing it" : "not a guarded block, passing it on");
		if (dsMemory)
			return;
		__real_free(p);
		return;
	}
	Lock();
	what = Check(b);
	if (what)
		Fail(b, what, caller);
	if (b->prev)
		b->prev->next = b->next;
	else
		live = b->next;
	if (b->next)
		b->next->prev = b->prev;
	b->magic = DEAD_MAGIC;
	blocks--;
	if (++operations % SWEEP_EVERY == 0)
		Sweep(caller);
	Unlock();
	__real_free(b->base);
}

void *__wrap_malloc(size_t size) { return Allocate(size, __builtin_return_address(0)); }

void __wrap_free(void *p) { Release(p, __builtin_return_address(0)); }

void *__wrap_calloc(size_t count, size_t size)
{
	size_t total = count * size;
	void *p = Allocate(total, __builtin_return_address(0));

	if (p)
		memset(p, 0, total);
	return p;
}

void *__wrap_realloc(void *p, size_t size)
{
	struct Block *b;
	void *fresh;

	if (!p)
		return Allocate(size, __builtin_return_address(0));
	b = (struct Block *)p - 1;
	Lock();
	{
		const char *what = Check(b);
		if (what)
			Fail(b, what, __builtin_return_address(0));
	}
	Unlock();
	if (size <= b->size) {
		/* Shrinking in place would move the trailer, so the simplest correct thing is to keep the
		 * block as it is: the caller asked for no more than it already has. */
		return p;
	}
	fresh = Allocate(size, __builtin_return_address(0));
	if (!fresh)
		return NULL;
	memcpy(fresh, p, b->size);
	Release(p, __builtin_return_address(0));
	return fresh;
}

/* memalign has to be wrapped too, or a block it returned would reach __wrap_free without a header.
 * The alignment is honoured by over-allocating and placing the header immediately before the aligned
 * payload, with the real base kept just before that so free can recover it. */
void *__wrap_memalign(size_t alignment, size_t size)
{
	size_t rounded = (size + 3u) & ~3u;
	unsigned char *base;
	unsigned char *payload;
	struct Block *b;

	if (alignment < 4)
		alignment = 4;
	base = (unsigned char *)__real_memalign(alignment, alignment + sizeof(struct Block) + rounded +
	                                                   sizeof(unsigned));
	if (!base)
		return NULL;
	/* The first aligned address that leaves room for a header before it. */
	payload = base + alignment + sizeof(struct Block);
	payload -= (uintptr_t)payload % alignment;
	b = (struct Block *)payload - 1;
	b->magic = HEAD_MAGIC;
	b->size = size;
	b->caller = __builtin_return_address(0);
	b->base = base;
	*Tail(b) = TAIL_MAGIC;

	Lock();
	b->prev = NULL;
	b->next = live;
	if (live)
		live->prev = b;
	live = b;
	blocks++;
	Unlock();
	return payload;
}

/* Where the live memory came from: the eight call sites holding the most, biggest first.
 *
 * This is how a leak gets a name. The guard already records which address asked for every block, so
 * the live list only has to be walked and totalled by caller. Run the result through
 * arm-vita-eabi-addr2line against the .elf to turn each address into a function. */
void VitaNativeHeapGuardReport(void)
{
	struct { const void *caller; unsigned bytes, count; } top[8];
	unsigned found = 0;

	memset(top, 0, sizeof top);
	Lock();
	for (struct Block *b = live; b; b = b->next) {
		unsigned i;

		for (i = 0; i < found; i++)
			if (top[i].caller == b->caller)
				break;
		if (i == found) {
			/* Not seen yet: take a free slot, or displace the smallest if it is bigger. */
			if (found < 8)
				i = found++;
			else {
				unsigned smallest = 0;
				for (unsigned j = 1; j < 8; j++)
					if (top[j].bytes < top[smallest].bytes)
						smallest = j;
				if (top[smallest].bytes >= b->size)
					continue;
				i = smallest;
				top[i].bytes = top[i].count = 0;
			}
			top[i].caller = b->caller;
		}
		top[i].bytes += b->size;
		top[i].count++;
	}
	Unlock();

	VitaNativeMemLog("[HEAP] %u live blocks; the callers holding the most:", blocks);
	for (unsigned i = 0; i < found; i++)
		VitaNativeMemLog("[HEAP]   %p  %u bytes in %u blocks", top[i].caller, top[i].bytes,
		                 top[i].count);
}

/* For the memory report: how many blocks the guard is holding. */
unsigned VitaNativeHeapGuardBlocks(void) { return blocks; }
