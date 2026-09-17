/* mi_hwaddr.c - DS hardware-window fixup for the SDK memory routines.
 *
 * Translated game code sometimes builds a DS VRAM/palette/OAM address
 * arithmetically (the battle engine clears all four VRAM banks with
 * `mov r1,#6; lsl r1,#24` -> MIi_CpuClear32) instead of through a literal
 * pool, so translate2.py's literal mapping never sees it and the raw DS address
 * reaches the SDK port, which faults at 0x06000000. Every MI copy/fill/clear
 * entry the build links is wrapped here and any pointer inside one of the
 * windows the SDK port keeps in arrays (the same table as translate2.py's
 * _HW_WINDOWS) is redirected to that array. Everything else passes through.
 */
#include <nitro.h>
#include <stdint.h>

extern void VitaNativeMemLog(const char *fmt, ...);

extern u8 s_HW_MAIN_MEM_SYSTEM[], s_HW_BG_PLTT[], s_HW_OBJ_PLTT[], s_HW_DB_BG_PLTT[], s_HW_DB_OBJ_PLTT[];
extern u8 s_HW_BG_VRAM[], s_HW_DB_BG_VRAM[], s_HW_OBJ_VRAM[], s_HW_DB_OBJ_VRAM[], s_HW_LCDC_VRAM[];
extern u8 s_HW_OAM[], s_HW_DB_OAM[];

static const struct { u32 base, size; u8 *sym; const char *name; } sWindows[] = {
    { 0x027FFC00, 0x400, s_HW_MAIN_MEM_SYSTEM, "MAIN_MEM_SYSTEM" },
    { 0x05000000, 0x200, s_HW_BG_PLTT, "BG_PLTT" }, { 0x05000200, 0x200, s_HW_OBJ_PLTT, "OBJ_PLTT" },
    { 0x05000400, 0x200, s_HW_DB_BG_PLTT, "DB_BG_PLTT" }, { 0x05000600, 0x200, s_HW_DB_OBJ_PLTT, "DB_OBJ_PLTT" },
    { 0x06000000, 0x80000, s_HW_BG_VRAM, "BG_VRAM" }, { 0x06200000, 0x20000, s_HW_DB_BG_VRAM, "DB_BG_VRAM" },
    { 0x06400000, 0x40000, s_HW_OBJ_VRAM, "OBJ_VRAM" }, { 0x06600000, 0x20000, s_HW_DB_OBJ_VRAM, "DB_OBJ_VRAM" },
    { 0x06800000, 0xA4000, s_HW_LCDC_VRAM, "LCDC_VRAM" },
    { 0x07000000, 0x400, s_HW_OAM, "OAM" }, { 0x07000400, 0x400, s_HW_DB_OAM, "DB_OAM" },
};

static unsigned sLogged;

static void *Fix(const void *p, const char *who)
{
    uintptr_t v = (uintptr_t)p;
    unsigned i;
    for (i = 0; i < sizeof(sWindows) / sizeof(sWindows[0]); i++) {
        if (v >= sWindows[i].base && v < sWindows[i].base + sWindows[i].size) {
            if (sLogged < 16) {
                sLogged++;
                VitaNativeMemLog("[MI-HW] %s: DS %08x -> %s+0x%x", who, (unsigned)v, sWindows[i].name,
                                (unsigned)(v - sWindows[i].base));
            }
            return sWindows[i].sym + (v - sWindows[i].base);
        }
    }
    return (void *)p;
}

extern void __real_MIi_CpuClear32(u32 data, void *dest, u32 size);
void __wrap_MIi_CpuClear32(u32 data, void *dest, u32 size) { __real_MIi_CpuClear32(data, Fix(dest, "MIi_CpuClear32"), size); }
extern void __real_MIi_CpuClear16(u16 data, void *dest, u32 size);
void __wrap_MIi_CpuClear16(u16 data, void *dest, u32 size) { __real_MIi_CpuClear16(data, Fix(dest, "MIi_CpuClear16"), size); }
extern void __real_MIi_CpuClearFast(u32 data, void *dest, u32 size);
void __wrap_MIi_CpuClearFast(u32 data, void *dest, u32 size) { __real_MIi_CpuClearFast(data, Fix(dest, "MIi_CpuClearFast"), size); }
extern void __real_MIi_CpuCopy16(const void *src, void *dest, u32 size);
void __wrap_MIi_CpuCopy16(const void *src, void *dest, u32 size) { __real_MIi_CpuCopy16(Fix(src, "MIi_CpuCopy16"), Fix(dest, "MIi_CpuCopy16"), size); }
extern void __real_MIi_CpuCopy32(const void *src, void *dest, u32 size);
void __wrap_MIi_CpuCopy32(const void *src, void *dest, u32 size) { __real_MIi_CpuCopy32(Fix(src, "MIi_CpuCopy32"), Fix(dest, "MIi_CpuCopy32"), size); }
extern void __real_MIi_CpuCopyFast(const void *src, void *dest, u32 size);
void __wrap_MIi_CpuCopyFast(const void *src, void *dest, u32 size) { __real_MIi_CpuCopyFast(Fix(src, "MIi_CpuCopyFast"), Fix(dest, "MIi_CpuCopyFast"), size); }
extern void __real_MI_CpuCopy8(const void *src, void *dest, u32 size);
void __wrap_MI_CpuCopy8(const void *src, void *dest, u32 size) { __real_MI_CpuCopy8(Fix(src, "MI_CpuCopy8"), Fix(dest, "MI_CpuCopy8"), size); }
extern void __real_MI_CpuFill8(void *dest, u32 data, u32 size);
void __wrap_MI_CpuFill8(void *dest, u32 data, u32 size) { __real_MI_CpuFill8(Fix(dest, "MI_CpuFill8"), data, size); }
extern void __real_MI_DmaCopy16(u32 dmaNo, const void *src, void *dest, u32 size);
void __wrap_MI_DmaCopy16(u32 dmaNo, const void *src, void *dest, u32 size) { __real_MI_DmaCopy16(dmaNo, Fix(src, "MI_DmaCopy16"), Fix(dest, "MI_DmaCopy16"), size); }
extern void __real_MI_DmaCopy32(u32 dmaNo, const void *src, void *dest, u32 size);
void __wrap_MI_DmaCopy32(u32 dmaNo, const void *src, void *dest, u32 size) { __real_MI_DmaCopy32(dmaNo, Fix(src, "MI_DmaCopy32"), Fix(dest, "MI_DmaCopy32"), size); }
extern void __real_MI_DmaFill32(u32 dmaNo, void *dest, u32 data, u32 size);
void __wrap_MI_DmaFill32(u32 dmaNo, void *dest, u32 data, u32 size) { __real_MI_DmaFill32(dmaNo, Fix(dest, "MI_DmaFill32"), data, size); }
extern void __real_DC_FlushRange(const void *start, u32 size);
void __wrap_DC_FlushRange(const void *start, u32 size) { __real_DC_FlushRange(Fix(start, "DC_FlushRange"), size); }
