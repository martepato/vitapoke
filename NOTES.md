# pspoke engineering notes

Lessons from porting Pokémon Platinum and SoulSilver to the PSP. Most of these cost a hardware round-trip or a lost
day; they are written down so nobody rediscovers them. Paths refer to `port/` unless stated.

## 1. Real PSP vs PPSSPP: the differences that bit us

1. **Open-file limit.** A real PSP refuses somewhere past ~10-16 simultaneously open Memory Stick files; PPSSPP has no
   limit. Platinum's overworld held 16 ROM files open (one host `fopen` each) and crashed to the XMB. Fix: one shared
   handle for the ROM, a per-file position, and a seek before every read (`native-audio-app/services/romfs.c`).
   SoulSilver's decompilation has the same shape (`GetNarcMemberSizeByIdPair` never closed its file). Any new game must
   start with the shared-handle ROM file system.
2. **`printf` is invisible on hardware.** The only evidence is a file next to the EBOOT, written open/append/close per
   line so it survives a crash (`VitaNativeMemLog`, `native-memlog.txt`). Log scene entries, overlay loads, ROM read
   failures, an fps line every 600 frames, and a `[FATAL]` line before every abort. A line without a newline before an
   abort reads as a hang.
3. **Hardware is 10-15% slower than PPSSPP.** Platinum: locked 30 fps in PPSSPP, 26-27 on a PSP-3001 in the heaviest
   city. Budget decisions must use the hardware number; emulator timings only compare builds against each other.
4. **Unaligned access and bad addresses:** PPSSPP's JIT tolerates them silently, hardware faults. Run headless
   PPSSPP with `-i` (interpreter): it logs `Read Word: SIGSEGV at <addr> ... PC <pc> RA <ra>` with a MIPS call
   stack. Resolve PC against `psp-nm -n <elf>` with load base 0x08804000.
5. **Extracted data blocks must be word-aligned.** Generated `.section` blocks without `.balign 4` get placed at odd
   addresses and the first 32-bit read faults on hardware (SoulSilver: 20 objects in 11 files). Check: symbol
   address % 4 must equal the DS address % 4.
6. **The DS naming screen kills real hardware** (black screen, power off; cause never found). Both games use the PSP
   firmware on-screen keyboard instead (`osk.c`, `naming_osk*.c`). The firmware OSK draws into whatever
   `sceGuDrawBufferList` last selected, not the published buffer, and headless PPSSPP does not render firmware
   dialogs at all, so the OSK can only be judged on hardware.
7. **sceSasCore needs double-buffered output.** The mixed audio grain was written into one buffer that
   `sceAudioOutputPannedBlocking` had just been handed; the hardware was still reading it while the next grain was
   mixed into it. PPSSPP copies the buffer immediately, so the emulator never showed it; a real PSP gave static or
   silence depending on unrelated code timing (dev builds happened to work, release builds did not). Fix in
   `native-audio-sound/sas_out.c`: two alternating grain buffers, `sceKernelDcacheInvalidateRange` before mixing and
   `sceKernelDcacheWritebackInvalidateRange` before output; converted sample buffers are flushed once after conversion.
8. **Stack was never the problem** (8.8 KB used of 256 KB). Do not chase it again.
9. **The Media Engine cannot be used** from this kind of user-mode homebrew: on a PSP-3001/6.61 with a kernel helper
   the ME reset vector runs but never reaches C (`test_out/me-probe` in the original workspace). Everything must get
   cheaper on the main CPU. sceSasCore itself works fine from user mode.
10. **Cache lines matter when hardware reads RAM.** Anything handed to the GE, the audio hardware or a kernel mixer must
    be written back (`sceKernelDcacheWritebackRange`); anything hardware writes must be invalidated before the CPU
    reads it. The emulator hides both mistakes.

## 2. The PSP firmware loader

- The retail loader rejects an EBOOT whose sections end past 32 MiB (error 80020148). `scripts/check_native_pbp.py`
  runs on every build; SoulSilver's game data lives in a `.native.backing` section split off by its linker script.
- `PARAM.SFO` `MEMSIZE=1` requests the large memory layout; both games set their own `DISC_ID`.

## 3. Porting a pret decompilation: pitfalls

1. **`void` functions that return a value.** The decompilation declares some functions `void` although the original
   leaves a meaningful value in r0 (via a tail call); translated assembly callers branch on it. GCC compiles the body to
   nothing and the caller tests garbage. Worse: non-void functions with no `return` statement (they relied on r0). Scan
   every unit with `-fsyntax-only -Wreturn-type` and fix the owned copy.
2. **Struct-by-value returns from translated assembly into C.** A hidden result pointer becomes the first register
   argument on both ARM and MIPS o32; counting declared parameters gives the wrong arity.
3. **Structs passed by value into translated code** are hidden incoming stack arguments (`stack-args.json`), and
   psp-gcc passes large structs by reference: the C caller has to hand the words over explicitly.
4. **Address-derived labels are ambiguous across overlays that share a load region.** Check that the call's register
   setup matches the named function's prototype; retarget with `call-retargets.json`.
5. **`bl` to a label inside the same function** (MWCC shared epilogue) is a `goto`, not a call.
6. **Fixed DS hardware addresses** in literal pools (VRAM, OAM, palettes, the 0x027FF... system area) are mapped to the
   SDK port's backing arrays; addresses built arithmetically (`mov r1,#6; lsl r1,#24`) reach the SDK raw, so the SDK
   memory/DMA entry points are wrapped (`--wrap=MIi_CpuCopy32` etc.) to redirect them. Three literal DS addresses also
   exist in the decompiled C (`battle_system.c`).
7. **SDK-port stubs that assert.** The X86 SDK port leaves nine fx matrix helpers as "Not implemented"; SoulSilver's
   starter screen calls one every frame. C ports live in `nitromain-perf/fx_mtx_native.c`.
8. **`MI_CpuFillFast`/`MI_CpuClearFast` round up to whole words.** The DS assembly stores words while the pointer is
   below `dest + size`; a port that did `size / 4` dropped 1-3 byte fills, and a 1-byte `MI_CpuClearFast` of a state
   variable is why a switched-in Pokémon had no health bar. Match the hardware semantics exactly.
9. **Decompiled "rodata trick" structs are undefined behaviour under GCC -O2** (a `u8[1]` indexed past its end to
   reach the next array). Look for `-Warray-bounds` warnings in the game compile log.
10. **`G2x_*` SDK entries have a different ABI under `SDK_PORT`** (`u64` address parameter): translated callers need
    DS-ABI twins (`g2_asm_abi.c`).
11. **DS anti-piracy checks (`DSProt_*`)** live in an encrypted overlay: `dsprot_native.c` returns the genuine-cartridge
    answers.
12. **Literal DS I/O register reads in C.** `unk_02013534.c` (text drawn as sprites) read DISPCNT through
    `*(vu32 *)0x04000000` / `0x04001000` to pick the sprite VRAM mapping mode. On the PSP that address is the GE's
    video memory, so the "mode" came from framebuffer pixels: battle move names were garbled until the pixels at that
    address happened to change (which is why the bug "fixed itself" after menu clicks). Use the SDK port's register
    variables (`reg_GX_DISPCNT`, `reg_GXS_DB_DISPCNT`). Sweep any decompilation for `0x0400xxxx` pointer casts, not
    just the VRAM range.
13. **Text and the sound heap:** `SDK_PORT` sound-heap blocks carry a larger header than on DS, so SoulSilver's
    DS-sized `heap_buf` runs out (Pokéathlon intro music never loads). Give it a bigger heap, same layout.

## 4. Rendering

- The 3D path is the DS display-list decoder feeding the GE. Outdoors the game submits ~270 display lists a frame;
  decoding and transforming them on the CPU was 17-18 ms of a 33 ms frame. The display-list cache
  (`native-stack-render/g3_listcache.h`) replays unchanged lists as one GE draw; fixed-point fast paths, a lazy clip
  matrix and word-wise compares brought outdoor game time from 19 to 5 ms in PPSSPP.
- Texture change detection uses VRAM write generations (`vram_dirty.c` + `opttex_redirect.py`) instead of comparing
  texture bytes every bind.
- Per-op command lists are borrowed from the stack instead of malloc/free (`OPT_CMDLIST_BORROW` in the SDK's
  `gecom.c`/`sbc.c`): ~560 allocations per frame gone.
- Textures larger than 512 px must be downscaled for the GE; SoulSilver's 4 MB heap has no 2 MB block for a full-size
  decode, so the downscale decodes source rows in pairs (`render-fastcompare/texture_cache.h`).
- The 2D compositor (derived from melonDS) converts BG tiles into per-layer atlases every frame and OBJ tiles lazily
  per frame generation. Mirrored texture wrap (repeat + flip) is composed in software because the GE has no
  mirrored mode.
- Layout: the main DS screen fills the PSP's full 272 px height (363x272, nearest-neighbour); the touch screen is a
  117x88 panel on the right, swapped into the big slot in stylus mode. The firmware OSK and the DEV fps counter draw
  with `pspDebugScreen` onto the published buffer.

- The DS box test (G3X_BoxTest) decides whether the game draws a map prop at all. The libntr PC simulator this
  port descends from ships `ClipSegment` commented out inside the box test's polygon clipper, so any bounding box
  with no corner inside the view (a conveyor belt longer than the screen) got a verdict from uninitialised stack
  data: Oreburgh City's belts vanished for a few steps and came back. Small props never showed it. Enabling the
  real interpolation (it was already in the file) fixed it; the fast path that answers the unambiguous cases
  without clipping is still valid. Anything inherited from a PC simulator that "works on PC" may only work because
  the PC build never depended on the answer.

- Menu/Bag scroll lag on hardware: every list-cursor step reloads that item's icon from the ROM, and the NARC
  reader re-parses the archive's BTAF/BTNF/GMIF header (5-6 tiny reads at fixed offsets) *every* member load, so
  one bag step is ~6-8 `fseek`+`fread`s on the 128 MB ROM on the Memory Stick — seek-dominated, invisible on the
  emulator (fast host reads) but a visible stutter on hardware. Fix: a bounded LRU of 16 KiB ROM-aligned blocks in
  the romfs `FS_ReadFile` (`native-audio-app/services/romfs.c`; SoulSilver has its own `soulsilver-native-core/
  nitromain-perf/romfs.c`), keyed on absolute ROM offset. ROM is read-only so blocks never go stale (cleared on
  `FS_End`). 32×16 KiB = 512 KiB static BSS. Emulator read-count: ~93% fewer ROM reads, 98% hit rate. Any repeated
  ROM read benefits, not just the bag. `-DNO_ROM_CACHE` disables it.
- The same cache **silenced SoulSilver audio on hardware** while Platinum was fine. SS's original `FS_ReadFile`
  called `clearerr(romStream)` before every read; the cached path returned before that line, so a sticky EOF/error
  flag (set by real Memory-Stick I/O) made `fread` return 0 and the cache stored that block as valid with length 0.
  Sound banks loaded as nothing; graphics unaffected. The silent build keys voices normally in headless PPSSPP --
  the host `fread` never sets the flag -- so this does not reproduce off-hardware. Fix: `clearerr` before each block
  read and never cache a block where `got != want`. Rule: any change to the ROM read path is hardware-test-only.

- **Draw order matters: the DS renders every opaque polygon of a frame first, then the translucent ones in
  submission order, and translucent polygons write depth only when polygon attribute bit 11 asks.** Both renderer
  copies drew in submission order with depth writes on, so a translucent prop submitted before the ground under it
  (Floaroma's south gate, signs) blended against the black clear and then occluded the ground: a solid black block
  instead of a soft shade (worse in evening light, where the DS shade is darker). Fixed by parking translucent
  batches with their GE state and drawing them at the end of the 3D frame in Present (2026-09-16, both games,
  hardware-confirmed). The `floaroma-gate` scenario captures the spot. Rule: when a translucent thing is solid black
  or missing, check ordering and depth writes before suspecting blending.
- The SoulSilver-only build used to fail at `ss-services` because the shared services include the overlay-id header
  that only the Platinum pipeline generated (and that generator reads the Platinum ROM). `generate.py
  --headers-only` writes just the headers and `soulsilver.sh` runs it when they are missing. Fresh-clone builds of
  each game alone are part of the release check now.

## 5. Audio

- The DS sound engine (sequencer, channels) runs fully on the CPU; output goes through `sceSasCore`, the PSP's own
  32-voice mixer: each DS channel becomes a PCM voice (ADPCM decoded once and cached), pitch = 16756991 / timer, volume
  from the DS divider. `__sceSasSetADSRmode` needs an even attack curve and odd decay/release curves or it rejects the
  whole call silently; default envelope rates are 0 (silent), so set a flat envelope explicitly.
- The channel timers count a 16.757 MHz clock (ARM7 / 2); one 5.21 ms sound pump is 87296 timer units. Feeding ARM7
  cycles (174592) advances channels twice as fast.
- The older CPU mixer stepped samples at 524 clock units per output sample for a 16 kHz output: that is an octave low
  (1047 is right). It is kept only as a fallback; the CPU mixer costs ~6x more main-thread time than sceSas.
- See §1.7 for the double-buffering bug.
- Battle BGM "dragging" while sound effects stayed in sync was not a latency/buffering problem. The audio thread
  plays a fixed number of pumps behind the game and holds its clock when it runs out of snapshots; battle frames
  were running long enough (40-45 ms) for it to run dry constantly. The long frames came from synchronous ROM reads
  (asset loads) on the game thread; the ROM block cache removed them and the BGM held tempo. Raising
  `LATENCY_PUMPS` would only have hidden it (and adds SFX latency) -- leave it at 10. Rule: when BGM drags but SFX
  are on time, look at frame time, not the mixer.
- Hand-assembled DEV diagnostic EBOOTs (rebuilding single objects with `make DEV=1` in individual work-tree
  directories, swapping `sas_out.o`, etc.) came out **silent on hardware** while playing normally in the emulator; the
  same tree built with the official `./build.sh platinum --dev` had sound. Never hand-mix objects for a build that
  goes on a card; use the official entry point so every object is built with one consistent flag set.
- Headless PPSSPP reports `[AUDIO-SAS] ... keyed N` even without audio output, which is a good logic check -- but a
  build that keys voices in the emulator can still be silent on the PSP (both the hand-built diagnostics and the
  SoulSilver cache bug were like this). Audio changes are hardware-test-only; the emulator only proves the logic.
- **Platinum audio can be silent on hardware for particular memory layouts, with no code change at all** (2026-09-16).
  A fresh clone in a long directory built an EBOOT that was silent on the PSP-3001 while the repo-path build of the
  same commit had sound. The only difference was 11 `__FILE__` strings (SDK assert messages compiled from absolute
  paths) growing by 84 bytes each, shifting `.data`/`.bss` by 0x380. Facts established on hardware, all with
  otherwise byte-identical code: a synthetic 0x390 `.rodata` pad reproduces the silence; shifts of 0x180, 0x200,
  0x500 have sound, 0x300 and 0x380 are silent; a 4 KB smaller heap (moving only post-heap kernel allocations)
  stays silent; allocating the SAS sample blocks with `PSP_SMEM_High` instead of `PSP_SMEM_Low` cures the silent
  layout; the same EBOOT keys voices normally in PPSSPP; poisoning all free memory at boot does not reproduce it in
  the emulator; a DEV build with the layout shifted by exactly one page and sample-integrity checks (cached and
  uncached re-checksums of every playing sample) had sound and no mismatches. `sceSasCore` runs on the main CPU
  (sc_sascore.prx), so it is not a Media Engine reach problem. Root cause still open (tracked in the issues).
  Mitigation: the SDK/overlay compile scripts now map the work-tree root to a fixed same-length token
  (`-ffile-prefix-map`), so every clone builds the byte-identical program that was hardware-tested. Rule: any
  Platinum code change re-rolls the layout, so each release needs one hardware sound check; and when a build is
  silent on hardware but fine in PPSSPP, compare section tables and symbol addresses against the last good build
  before suspecting the diff. The hand-built DEV silence above was probably this same thing.

## 6. Testing loop

- `run_probe.py` (both apps) stages a throwaway memory stick, symlinks the ROM read-only, creates the save exclusively
  and checks hashes afterwards. A copy of a real save (`--save`) proves the hardware save loads.
- Headless PPSSPP renders no firmware dialogs and has no audio. The audio work used a locally patched headless build
  that writes the mixed output to a WAV; compare recordings by envelope correlation and per-chunk pitch ratio.
- Replays (`input-*.txt`, `frame buttons x y`, buttons in 8-digit hex, frames strictly increasing) get a fixed RTC so
  the RNG is deterministic. PSP bits: UP 0x10, RIGHT 0x20, DOWN 0x40, LEFT 0x80, CIRCLE(A) 0x2000, CROSS(B) 0x4000,
  TRIANGLE 0x1000, START 0x8. HGSS walks ~8 frames per tile; map scripts close the movement gate for ~400 frames after
  a warp. Advance dialogue with B, not an A-mash. `make WARP_TO=<map>,<x>,<z>` warps without walking; `NO_WILD=1`
  keeps exploration replays battle-free.
- A replay that "passes" proves nothing until it is shown to reach the target: check loaded overlay ids, the coordinate
  trail and a screenshot. Frame dumps (`VITAPOKE_FRAME_DUMP`) of both DS screens are the fastest way to see what the
  game drew at a given frame.
- Reproducibility: EBOOT md5s differ between trees because asserts embed paths; compare text symbol sizes with
  `psp-nm -S` instead.
- `tests/run.sh` is the gate above turned into a repo script. Its first full run caught a shipped bug: the SoulSilver
  repel prompt asserted in `RunScriptCommand` because the script command table (`scrcmd_c.c`, not rebuilt by the QoL
  step) never got commands 853/854, and because the patch wrote `#if VITAPOKE_QOL_REPEL_PROMPT` *before* including the
  header that defines it. A feature guarded by a header macro must include that header first, in every file that
  tests the macro, and every file a patch touches (including through headers) must be in the rebuild list. "Symbols
  identical to the card build" proves nothing when the card build has the same bug.

## 7. Process rules

- Never touch a real save or ROM from scripts; the install script only copies the EBOOT and creates a blank save when
  none exists.
- Never `rm *.o` blindly: several objects are hand-placed prebuilt files with no make rule.
- Before deleting anything on a card or in a workspace, list exactly what will go and check it against what must stay;
  `ls` output with trailing slashes once defeated a keep-list and deleted folders that should have stayed.
- Keep a card log backup before every install; the previous EBOOT too.

## 8. Memory

- Measure with `psp-size` on the linked ELF (text/data/bss) and `psp-nm --size-sort -S` for the biggest symbols; the
  `--dev` build's `[MEM]` log lines give heap peak on hardware. Do not trust `sceKernelTotalFreeMemSize` alone: it
  reports what is left inside the 8 MB malloc partition, not the whole picture.
- After trimming, each game is ~6 MB code + ~23 MB BSS, plus an 8 MB malloc heap (`PSP_HEAP_SIZE_KB 8192`, measured
  peak use ~3.5 MB), ~1 MB thread stacks and 2 MB GE VRAM (separate). About 38 MB of main RAM on a PSP-2000+/3000.
  The BSS is dominated by the emulated DS memory map the port keeps as native shadows: `s_HW_MAIN_MEM` 8 MB,
  `s_HW_MAIN_MEM_SUB` 4 MB, the VRAM banks ~1.7 MB, 2 MB of GE display lists, 1.5 MB sound system, ~1.3 MB
  renderer caches, 0.5 MB ROM block cache.
- `s_HW_MAIN_MEM_EX` (8 MB) was dead: it is the DSi/TWL extended main RAM, these are NTR (`SDK_4M`) games, and
  nothing referenced the buffer -- only the `HW_MAIN_MEM_EX_SIZE` constant, in an nvram bounds check. It came from
  reusing libntr's PC-simulator memory map (`hw/X86/mmap_global.h`, sized for the debug/DSi case) instead of retail
  sizes. Removed in the backing generator (`native-probe/backing/generate.py`) rather than shrunk, so any real
  reference fails at link. Check for this class of waste with `psp-nm -A` over every archive: a defined symbol with
  no `U` reference anywhere is a candidate.
- `s_HW_MAIN_MEM` at 8 MB is **not** waste even though retail main RAM is 4 MB: the nvram DMA validator accepts any
  address in `[HW_MAIN_MEM, HW_MAIN_MEM + 8 MB)`, i.e. the DS main-RAM mirror region. Shrinking the buffer without
  changing that validator lets DMA target memory past the buffer and corrupt whatever follows -- silently, and only
  on hardware. Any change to the memory map must be hardware-tested; the emulator's memory layout tolerates what a
  real PSP does not.
- `PSP_LARGE_MEMORY = 1` (both Makefiles) is what grants the 64 MB model; a PSP-1000 has 32 MB total and no such
  region. With the 6 MB code image fixed and ~12 MB of DS memory shadows that the game code addresses directly,
  the realistic floor is ~32 MB, so a PSP-1000 port is a memory-map re-architecture, not a diet. See issue #10.
- Keeping the old EBOOTs from every card install (DSonPSP/native-archive/hardware-logs/card-*) turned a mystery
  regression into a three-step hardware bisect. Archive before every install.
