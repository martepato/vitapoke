/* vitapoke: the DS's firmware profile and its lock IDs, on the Vita.
 *
 * A DS keeps the owner's name, birthday and language in firmware, and the game reads them: the
 * birthday decides which Pokémon greet you on your birthday, and the language picks the text. There
 * is no such thing on this console, and no settings screen in this port to ask for one, so the
 * profile below is baked in -- the same English profile the PSP build used and that a DS emulator's
 * generated firmware provides.
 *
 * The lock IDs are the DS SDK's arbitration between the ARM9 and the ARM7 over the cartridge bus.
 * Nothing here has two processors, but the SDK's own code takes and releases them, so they are
 * handed out for real: an ID that was never allocated must not appear to be free.
 */
/* stdlib.h before nitro.h: libntr's nitro/card/backup.h calls malloc and free without
 * declaring them, and relies on the including file having done it. */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <nitro.h>

#include "include/vita_os.h"

void OS_GetOwnerInfo(OSOwnerInfo *p)
{
	if (!p)
		VitaNativeFatal("OS_GetOwnerInfo with no destination");
	memset(p, 0, sizeof(*p));
	p->language = 1;                /* English */
	p->birthday.month = 1;
	p->birthday.day = 1;
	p->nickName[0] = 'V';
	p->nickName[1] = 'I';
	p->nickName[2] = 'T';
	p->nickName[3] = 'A';
	p->nickNameLength = 4;
}

/* The DS stores the offset between its RTC and the owner's chosen time zone here. port/vita/input.c
 * already reports the console's local time, so there is nothing left to correct for. */
s64 OS_GetOwnerRtcOffset(void) { return 0; }

/* The DS's wireless MAC address. Wi-Fi and DS wireless are not ported and are not planned, so
 * nothing can be reached with this; it exists because the SDK reads it during startup. A
 * locally-administered address (bit 1 of the first byte) is the honest answer: it is guaranteed not
 * to collide with a real adapter. */
void OS_GetMacAddress(u8 *p)
{
	static const u8 address[6] = { 0x02, 0x56, 0x49, 0x54, 0x41, 0x01 };

	if (!p)
		VitaNativeFatal("OS_GetMacAddress with no destination");
	memcpy(p, address, sizeof address);
}

/* ---------------------------------------------------------------- lock IDs */

static uint64_t allocated;

s32 OS_GetLockID(void)
{
	s32 id = OS_LOCK_ID_ERROR;

	VitaOS_TableLock();
	for (unsigned i = 0; i < 48; i++) {
		if (!(allocated & (UINT64_C(1) << i))) {
			allocated |= UINT64_C(1) << i;
			id = OS_MAINP_LOCK_ID_START + i;
			break;
		}
	}
	VitaOS_TableUnlock();
	return id;
}

void OS_ReleaseLockID(u16 id)
{
	uint64_t bit;

	if (id < 0x40 || id > 0x6f)
		VitaNativeFatal("OS_ReleaseLockID with an ID outside the main processor's range");
	bit = UINT64_C(1) << (id - 0x40);
	VitaOS_TableLock();
	if (!(allocated & bit))
		VitaNativeFatal("OS_ReleaseLockID on an ID that was not allocated");
	allocated &= ~bit;
	VitaOS_TableUnlock();
}

u16 OS_ReadOwnerOfLockWord(OSLockWord *p)
{
	if (!p)
		VitaNativeFatal("OS_ReadOwnerOfLockWord with no lock word");
	return p->ownerID;
}

/* ---------------------------------------------------------------- the cartridge lock
 *
 * The lock word lives in the DS's shared memory, where both processors can see it. Here there is
 * only one, and the DS execution lock already keeps DS threads out of each other's way, so the table
 * lock is enough to make the test and the set one step.
 */
s32 OS_TryLockCartridge(u16 id)
{
	OSLockWord *p = (OSLockWord *)HW_CTRDG_LOCK_BUF;
	s32 prior;

	if (id < 0x40 || id > 0x7f)
		return OS_LOCK_ERROR;
	VitaOS_TableLock();
	prior = p->lockFlag;
	if (!prior) {
		p->lockFlag = id;
		p->ownerID = id;
	}
	VitaOS_TableUnlock();
	return prior;
}

s32 OS_LockCartridge(u16 id)
{
	s32 result;

	/* Spinning here would hold the DS execution lock and stop the thread that is going to release
	 * the cartridge, so wait the way the DS does: out of DS context. */
	while ((result = OS_TryLockCartridge(id)) > 0)
		OS_SpinWait(OS_SYSTEM_CLOCK / 10000);
	return result;
}

s32 OS_UnlockCartridge(u16 id)
{
	OSLockWord *p = (OSLockWord *)HW_CTRDG_LOCK_BUF;
	s32 result = OS_UNLOCK_SUCCESS;

	VitaOS_TableLock();
	if (p->ownerID != id) {
		result = OS_UNLOCK_ERROR;
	} else {
		p->ownerID = 0;
		p->lockFlag = 0;
	}
	VitaOS_TableUnlock();
	return result;
}

/* Both spellings are in the SDK's headers, and the game uses both. */
s32 OS_UnLockCartridge(u16 id) { return OS_UnlockCartridge(id); }
