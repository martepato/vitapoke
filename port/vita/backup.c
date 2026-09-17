/* vitapoke: the DS cartridge's save memory, as a file on the memory card.
 *
 * Platinum saves to 512 KB of flash on the cartridge. This is that flash: a 512 KB file, byte for
 * byte what a DS writes, so a save moves between this port, a cartridge dump and a DS emulator
 * without conversion.
 *
 * Two rules the PSP build set and this keeps, because they are what makes a save safe:
 *
 *   - the file is never created and never grown. It must already exist and be exactly 512 KB. A save
 *     file that is the wrong size is somebody else's file, or a truncated one, and writing the game's
 *     512 KB layout into it would destroy whatever it was.
 *   - a write is verified by reading it back when the SDK asks for verification, because that is what
 *     the DS's own backup code does and the game's save routine relies on the result.
 *
 * The SDK's interface is asynchronous. There is nothing asynchronous underneath here, so every
 * request completes before it returns and the wait calls have nothing to wait for. That is a
 * deliberate simplification rather than an oversight: the game always waits for the result, and a
 * save that takes a few milliseconds of blocking I/O is invisible next to one frame.
 */
/* stdlib.h before nitro.h: libntr's nitro/card/backup.h calls malloc and free without
 * declaring them, and relies on the including file having done it. */
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nitro.h>
#include <psp2/kernel/threadmgr.h>

#include "include/vita_os.h"
#include "include/vitapoke.h"

#define BACKUP_BYTES (512u * 1024u)

static FILE *file;
static SceUID gate = -1;
static int owner = -1;
static CARDResult result = CARD_RESULT_NO_RESPONSE;

BOOL VitaNative_OpenBackup(const char *path)
{
	FILE *candidate;

	if (file || owner != -1)
		return FALSE;
	candidate = fopen(path, "r+b");
	if (!candidate)
		return FALSE;
	if (fseek(candidate, 0, SEEK_END) || ftell(candidate) != (long)BACKUP_BYTES) {
		fclose(candidate);
		return FALSE;
	}
	gate = sceKernelCreateSema("vitapoke_backup", 0, 1, 1, NULL);
	if (gate < 0) {
		fclose(candidate);
		return FALSE;
	}
	file = candidate;
	result = CARD_RESULT_SUCCESS;
	return TRUE;
}

BOOL VitaNative_CloseBackup(void)
{
	int status;

	if (owner != -1 || !file)
		return FALSE;
	status = fclose(file);
	file = NULL;
	sceKernelDeleteSema(gate);
	gate = -1;
	result = status ? CARD_RESULT_FAILURE : CARD_RESULT_SUCCESS;
	return status == 0;
}

/* The DS SDK's lock around a sequence of backup requests. It blocks, so the DS execution lock comes
 * off across the wait: the thread that is going to unlock the backup has to be able to run. */
void CARD_LockBackup(u16 id)
{
	unsigned depth;
	int taken;

	if (gate < 0)
		VitaNativeFatal("CARD_LockBackup with no save file open");
	depth = VitaOS_Release();
	taken = sceKernelWaitSema(gate, 1, NULL);
	VitaOS_Reacquire(depth);
	if (taken < 0)
		VitaNativeFatal("CARD_LockBackup failed");
	owner = id;
}

void CARD_UnlockBackup(u16 id)
{
	if (owner != id)
		VitaNativeFatal("CARD_UnlockBackup by a thread that does not hold the lock");
	owner = -1;
	if (sceKernelSignalSema(gate, 1) < 0)
		VitaNativeFatal("CARD_UnlockBackup failed");
}

BOOL CARD_IdentifyBackup(CARDBackupType type)
{
	result = file && type == CARD_BACKUP_TYPE_FLASH_4MBITS ? CARD_RESULT_SUCCESS : CARD_RESULT_UNSUPPORTED;
	return result == CARD_RESULT_SUCCESS;
}

u32 CARD_GetBackupTotalSize(void) { return BACKUP_BYTES; }
u32 CARD_GetBackupSectorSize(void) { return 65536; }
u32 CARD_GetBackupPageSize(void) { return 256; }
CARDResult CARD_GetResultCode(void) { return result; }
BOOL CARD_TryWaitBackupAsync(void) { return TRUE; }
BOOL CARD_WaitBackupAsync(void) { return result == CARD_RESULT_SUCCESS; }
void CARD_CancelBackupAsync(void) { }   /* the work has already finished */

static BOOL Verify(u32 offset, const u8 *source, u32 length)
{
	u8 buf[256];

	if (fseek(file, offset, SEEK_SET))
		return FALSE;
	while (length) {
		u32 amount = length > sizeof buf ? sizeof buf : length;
		if (fread(buf, 1, amount, file) != amount || memcmp(buf, source, amount))
			return FALSE;
		source += amount;
		length -= amount;
	}
	return TRUE;
}

/* The one entry point the SDK's backup code funnels every read, write and verify through. */
BOOL CARDi_RequestStreamCommand(u32 src, u32 dst, u32 length, MIDmaCallback callback, void *arg,
                                BOOL async, CARDRequest request, int retry, CARDRequestMode mode)
{
	u32 offset = request == CARD_REQ_READ_BACKUP ? src : dst;
	BOOL ok;

	(void)async;
	(void)retry;
	result = CARD_RESULT_SUCCESS;
	if (!file || owner < 0) {
		result = CARD_RESULT_NO_RESPONSE;
	} else if (offset > BACKUP_BYTES || length > BACKUP_BYTES - offset) {
		result = CARD_RESULT_INVALID_PARAM;
	} else if (request == CARD_REQ_READ_BACKUP) {
		if (mode != CARD_REQUEST_MODE_RECV || (!dst && length))
			result = CARD_RESULT_INVALID_PARAM;
		else if (fseek(file, offset, SEEK_SET) || fread((void *)(uintptr_t)dst, 1, length, file) != length)
			result = CARD_RESULT_FAILURE;
	} else if (request == CARD_REQ_WRITE_BACKUP) {
		if ((mode != CARD_REQUEST_MODE_SEND && mode != CARD_REQUEST_MODE_SEND_VERIFY) || (!src && length))
			result = CARD_RESULT_INVALID_PARAM;
		else if (fseek(file, offset, SEEK_SET) ||
		         fwrite((const void *)(uintptr_t)src, 1, length, file) != length || fflush(file))
			result = CARD_RESULT_FAILURE;
		else if (mode == CARD_REQUEST_MODE_SEND_VERIFY && !Verify(offset, (const u8 *)(uintptr_t)src, length))
			result = CARD_RESULT_FAILURE;
	} else if (request == CARD_REQ_VERIFY_BACKUP) {
		if (mode != CARD_REQUEST_MODE_SEND || (!src && length))
			result = CARD_RESULT_INVALID_PARAM;
		else if (!Verify(offset, (const u8 *)(uintptr_t)src, length))
			result = CARD_RESULT_FAILURE;
	} else {
		result = CARD_RESULT_UNSUPPORTED;
	}
	ok = result == CARD_RESULT_SUCCESS;
	if (callback)
		callback(arg);
	return ok;
}
