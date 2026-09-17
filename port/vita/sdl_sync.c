/* vitapoke: the handful of SDL2 calls libntr makes, on psp2.
 *
 * libntr is written to build for desktop simulators as well as real DS hardware, and on any target
 * that is not SDK_BUILD_ARM its mutexes, message queues and alarms are declared in terms of SDL. That
 * is a property of the library, not of the PSP: the PSP build satisfies it the same way, with an
 * adapter over the PSP kernel and no SDL runtime anywhere.
 *
 * Only what libntr refers to is here. See sdl2-shim/SDL_stdinc.h for why the headers are local.
 */
#include <stdlib.h>

#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_timer.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

/* psp2 mutexes are recursive when asked, which is what SDL_LockMutex promises, so the depth and owner
 * bookkeeping the PSP adapter had to do by hand is the kernel's job here. */
struct SDL_mutex {
	SceUID id;
};

struct SDL_sem {
	SceUID id;
};

struct SDL_Thread {
	SceUID id;
	SDL_ThreadFunction fn;
	void *arg;
	int result;
};

SDL_mutex *SDL_CreateMutex(void)
{
	SDL_mutex *mutex = calloc(1, sizeof *mutex);
	if (!mutex)
		return NULL;
	mutex->id = sceKernelCreateMutex("vitapoke_sdl", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, NULL);
	if (mutex->id < 0) {
		free(mutex);
		return NULL;
	}
	return mutex;
}

int SDL_LockMutex(SDL_mutex *mutex)
{
	if (!mutex)
		return -1;
	return sceKernelLockMutex(mutex->id, 1, NULL) < 0 ? -1 : 0;
}

int SDL_UnlockMutex(SDL_mutex *mutex)
{
	if (!mutex)
		return -1;
	return sceKernelUnlockMutex(mutex->id, 1) < 0 ? -1 : 0;
}

void SDL_DestroyMutex(SDL_mutex *mutex)
{
	if (!mutex)
		return;
	sceKernelDeleteMutex(mutex->id);
	free(mutex);
}

SDL_sem *SDL_CreateSemaphore(Uint32 initial)
{
	SDL_sem *sem = calloc(1, sizeof *sem);
	if (!sem)
		return NULL;
	/* SDL semaphores have no ceiling; give it one high enough that libntr's use of them cannot
	 * reach it. */
	sem->id = sceKernelCreateSema("vitapoke_sdl", 0, (int)initial, 0x7fffffff, NULL);
	if (sem->id < 0) {
		free(sem);
		return NULL;
	}
	return sem;
}

int SDL_SemWait(SDL_sem *sem)
{
	if (!sem)
		return -1;
	return sceKernelWaitSema(sem->id, 1, NULL) < 0 ? -1 : 0;
}

int SDL_SemPost(SDL_sem *sem)
{
	if (!sem)
		return -1;
	return sceKernelSignalSema(sem->id, 1) < 0 ? -1 : 0;
}

void SDL_DestroySemaphore(SDL_sem *sem)
{
	if (!sem)
		return;
	sceKernelDeleteSema(sem->id);
	free(sem);
}

static int ThreadEntry(SceSize args, void *argp)
{
	SDL_Thread *thread = *(SDL_Thread **)argp;
	(void)args;
	thread->result = thread->fn(thread->arg);
	return 0;
}

SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data)
{
	SDL_Thread *thread;
	SDL_Thread *arg;

	if (!fn)
		return NULL;
	thread = calloc(1, sizeof *thread);
	if (!thread)
		return NULL;
	thread->fn = fn;
	thread->arg = data;
	/* Below the DS threads in os_thread.c: these are libntr's own helpers, not game threads, and
	 * nothing waits on them from inside DS code. */
	thread->id = sceKernelCreateThread(name ? name : "vitapoke_sdl", ThreadEntry, 0x80, 0x8000, 0,
	                                   SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
	if (thread->id < 0) {
		free(thread);
		return NULL;
	}
	arg = thread;
	if (sceKernelStartThread(thread->id, sizeof arg, &arg) < 0) {
		sceKernelDeleteThread(thread->id);
		free(thread);
		return NULL;
	}
	return thread;
}

void SDL_WaitThread(SDL_Thread *thread, int *status)
{
	int exitStatus = 0;

	if (!thread)
		return;
	sceKernelWaitThreadEnd(thread->id, &exitStatus, NULL);
	if (status)
		*status = thread->result;
	sceKernelDeleteThread(thread->id);
	free(thread);
}

void SDL_Delay(Uint32 ms)
{
	while (ms) {
		Uint32 part = ms > 1000000 ? 1000000 : ms;
		sceKernelDelayThread(part * 1000);
		ms -= part;
	}
}

Uint32 SDL_GetTicks(void) { return (Uint32)(sceKernelGetProcessTimeWide() / 1000ULL); }
