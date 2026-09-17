# Porting pspoke to the PS Vita

This is the working document for the Vita port: what has been decided, what has been proven, and what
is left. `NOTES.md` is the PSP equivalent and still applies to anything shared.

The Vita can already run the PSP build through Adrenaline. This port is a different thing: a native
Vita application built with VitaSDK, for an ARM CPU with four cores and 512 MB of RAM instead of a
single MIPS core with 64 MB.

## Why it is worth doing

The PSP build is shaped by three of that console's limits, and the Vita has none of them.

- **Memory.** pspoke needs about 38 MB, which is why it cannot run on a PSP-1000 at all
  ([issue #10](https://github.com/IbrahimIrfan/pspoke/issues/10)) and why the SoulSilver link script has
  to split `.native.backing` to stay inside the firmware loader's 32 MiB section bound. The Vita has
  512 MB, so the memory layout stops being a constraint on the design.
- **The screen.** Two 256x192 DS screens do not fit on a 480x272 display, so the PSP build gives the
  main screen 363x272 and shrinks the other to 117x88 in the corner. At 960x544 both fit at an integer
  2x with room to spare.
- **Touch.** The PSP has no touchscreen, so the stylus is a cursor driven by the analogue stick, and
  `R` is taken over to swap which screen is large. The Vita has a real touchscreen, so the DS touch
  screen can be touched.

Speed is a genuine open question rather than an assumed win. The PSP build runs at around 30 fps with a
hand-tuned GE backend; a Cortex-A9 at 444 MHz is far ahead of a 333 MHz Allegrex on the CPU side, but
the first Vita renderer will not be as tuned as the GE one is, and nothing here has run on hardware yet.

## What is proven

Everything in this section was built and linked with the pinned toolchain.

- **The toolchain.** `scripts/toolchain-vita.sh` downloads one immutable `vitasdk/autobuilds` snapshot,
  SHA-256 verified against that release's own `SHA256SUMS`, into `.cache/vitasdk` — the same shape as
  `scripts/toolchain.sh` does for PSPDEV. It deliberately does not use `vdpm` or
  `bootstrap-vitasdk.sh`: both resolve a channel through `vitasdk.org`, which makes the compiler you get
  depend on when you ran the build, and the point of pinning is that it does not.
- **The GPU dependencies.** `scripts/deps-vita.sh` builds vitaGL and math-neon at pinned commits into
  the toolchain. vitaGL is not the renderer's target (see the decision below), but it is how the port
  currently proves the GPU offers the texture formats the DS compositor needs.
  `port/vita/shark_stub.c` answers its runtime-shader-compiler calls with "no compiler", and the
  application links `SceShaccCg` weakly, so nothing here obliges a player to extract
  `libshacccg.suprx` from a firmware update. Keeping that true is a constraint on the renderer, not a
  finished property of it.
- **The platform layer.** `port/vita` implements the DS interfaces the game and libntr call -- `OS_*`,
  `TP_*`, `RTC_*`, the pad registers -- directly on psp2. See **Where the boundary is** below for why
  that, and not the PSPSDK, is the line. `tests/vita/ds_surface.c` calls every one of them and is
  compiled and linked by `tests/vita/run.sh`, which is what catches a psp2 or libntr rename after a
  toolchain bump.
- **Packaging.** `arm-vita-eabi-gcc -Wl,-q` → `vita-elf-create` → `vita-make-fself` → `vita-pack-vpk`
  produces a loadable VPK. Proven with the vitaGL feature check, not yet wired into a build.

Run it with:

```sh
./build-vita.sh setup     # pinned VitaSDK + vitaGL, about 100 MB, one time
./build-vita.sh check     # the checks that need no ROM
```

`check` fetches libntr (the DS SDK replacement) because the platform layer is compiled against its
headers.

`./build.sh platinum|soulsilver` still builds for the PSP and is unaffected.

## Where the boundary is

The game does not call the PSPSDK. It calls the DS SDK: `OS_Init`, `OS_GetTick`, `OS_WaitIrq`,
`OS_CreateThread`, `TP_Init`, `RTC_GetDateTime`, and the DS hardware registers. `platform.c`,
`input.c`, `alarms.c` and `threads.c` in the PSP tree are *implementations of that interface* on PSP
calls, and the `sce*` in them is an implementation detail of that console, not an interface the game
knows about.

So the Vita port implements the same DS interfaces on psp2, in `port/vita`. It speaks no PSPSDK
anywhere. The first attempt at this port did the other thing -- headers named `pspkernel.h` injected
ahead of the toolchain's, forwarding `sceKernelGetSystemTimeWide` to `sceKernelGetProcessTimeWide` --
so that the PSP's `platform.c` and `input.c` would compile unchanged. That was the wrong line to draw,
for reasons worth recording because they are easy to re-derive badly:

- **A PSP-shaped interface cannot express Vita hardware.** A shimmed `SceCtrlData` has `Buttons`, `Lx`
  and `Ly` and nowhere to put a touch point, so the single most important input difference between the
  two consoles has to be smuggled past the shim rather than expressed through it.
- **The stubs hid real questions.** `sceKernelCpuSuspendIntr` was a genuine critical section on one
  MIPS core, and `port/native-threads/threads.c` guards its thread tables with it. Forwarded to a
  no-op on a four-core Cortex-A9 it compiles clean and reads as handled, while being a data race. The
  DS execution lock below is the answer that question actually has.
- **It bought less than it looked like.** 93 of 391 sources in `port/` mention a PSP API at all, and
  the ones a shim lets you reuse unchanged -- `platform.c`, `input.c`, `main.c`, `frame.c` -- are
  exactly the ones that should differ per console. The other ~300 are DS, SDK and game code that never
  mentions the PSP and is shared either way.

What `port/vita` does depend on is libntr, which is a property of the decompilation rather than of any
console: on any target that is not `SDK_BUILD_ARM`, libntr's OS headers declare their mutexes and
alarms in terms of three SDL2 types. The PSP build satisfies that with PSPDEV's SDL2 headers and a
small adapter; VitaSDK ships no SDL2, so `port/vita/sdl2-shim` declares the five things libntr refers
to and `port/vita/sdl_sync.c` implements them on psp2 threads. It is not an SDL port and must not
become one.

### The DS execution lock

The DS is one core. Its threads take turns, never run at once, and protect shared state by stopping the
scheduler rather than by locking -- and the game and libntr are written to that. The PSP inherited the
guarantee for free: one core, plus `sceKernelSuspendDispatchThread` to stop a switch.

The Vita has four cores and a preemptive scheduler, so two DS threads genuinely run at once. That is a
behaviour change, not a porting detail, and it would show up as rare corruption rather than as a build
failure.

`port/vita` therefore makes the DS's guarantee explicit: one recursive lock, held by whichever DS
thread is running DS code, released exactly where the DS would have switched -- `OS_SleepThread`,
`OS_YieldThread`, `OS_Sleep`, `OS_JoinThread` and the vblank wait. The threads are real psp2 threads
and the kernel still schedules them; only one is ever inside DS code. `OS_DisableScheduler` then has
nothing left to do but count, because holding the lock is already the promise it was asking for.

Two other differences worth naming, both consequences of psp2 not having what the PSP did:

- psp2 has **no kernel alarms**, so `os_alarm.c` runs one timer thread that sleeps until the earliest
  deadline and holds the execution lock across each handler -- which is what makes `OS_GetProcMode`
  reporting `OS_PROCMODE_IRQ` inside a handler mean what it says.
- psp2 has **no sleep/wakeup counter** (`sceKernelWaitSignal` holds a single pending signal whose
  second send is an error), so each DS thread gets an event flag. A wake that lands between a thread
  queueing itself and sleeping leaves the flag set, so the sleep returns at once; losing that would
  hang the game, and it is the property `OS_SleepThread` is built on.

## The renderer: what the DS needs and what each console offers

This is the part of the port that decides the rest, so the findings are written out in full.

`port/native-stack-render` (Platinum) and `port/soulsilver-native-assets/render-fastcompare`
(SoulSilver) composite the DS 2D engines with the PSP's GE, falling back to the melonDS-derived software
renderer in `port/native-render-opt` for anything the GE path does not handle. The GE surface they use
is small and well bounded: **47 `sceGu`/`sceGe` functions and about 60 `GU_*` constants**, in six files.

Three properties of that design were the open questions.

- **Paletted textures — settled, and in the port's favour.** The DS stores backgrounds and sprites as
  4bpp or 8bpp tiles indexing a 16- or 256-entry palette, and the whole atlas design (`GU_PSM_T4`,
  `GU_PSM_T8`, `sceGuClutLoad`, `sceGuClutMode` with a bank offset) exists because the GE samples that
  format directly. Expanding every tile to true colour each frame instead would be a large amount of
  new per-frame work.

  GXM has `SCE_GXM_TEXTURE_BASE_FORMAT_P4` and `P8` with `sceGxmTextureSetPalette`, and vitaGL exposes
  them as `GL_COLOR_INDEX8_EXT` with `glColorTable`. **The atlas design carries over.** The one thing
  that does not map directly is `sceGuClutMode`'s bank offset, which selects a 16-entry sub-palette of a
  256-entry CLUT for 4bpp tiles: GXM has no bank register, so a bank becomes a choice between sixteen
  prebuilt 16-entry palettes, which is a pointer swap per draw rather than a register write.
- **Stencil and render-to-texture — available.** The compositor builds the DS window and layer-priority
  masks in the stencil buffer, and composes each engine and the 3D output into its own buffer before
  drawing them to the screen. vitaGL has stencil and FBOs, checked by `tests/vita/vitagl_features.c`.
- **`GU_SPRITES` — no equivalent.** The PSP draws a screen-aligned rectangle from two vertices, and the
  2D compositor leans on it for every tile: `DrawTextBG` emits one sprite per 8x8 tile. There is no such
  primitive in OpenGL or GXM, so whatever sits under the renderer has to expand each pair into two
  triangles. That is a real per-tile cost the PSP build does not pay.

### The hard part: EDRAM is a flat address space and the Vita has no equivalent

The PSP renderer treats the GE's 2 MiB of EDRAM as raw memory at fixed offsets from
`sceGeEdramGetAddr()`, and switches render target with `sceGuDrawBufferList` by passing an offset:

| Offset | Size | What it is |
|---|---|---|
| `0x000000` | `0x88000` | display buffer 0 (480x272, stride 512, 8888) |
| `0x088000` | `0x88000` | display buffer 1 (`displayOffset ^= 0x88000` flips) |
| `0x110000` | `0x30000` | 3D colour buffer (256x192, stride 256) |
| `0x140000` | `0x18000` | 3D depth buffer (stride 256) |
| `0x158000` | `0x30000` | engine A composite (256x192, stride 256) |
| `0x188000` | `0x30000` | engine B composite |

The same offsets are then read back as *textures* — `sceGuTexImage(0, 256, 256, 256, edram + VRAM_3D)`
composites the 3D buffer into engine A — and in one place read by the CPU, where `RenderEngineSoft`
pulls the 3D buffer back out of EDRAM to feed the software compositor.

On the Vita a render target is a GXM object rather than an address, so the flat-address assumption has
to be replaced by a table: each region becomes a GXM colour surface, and switching target becomes a
lookup of the region a pointer falls in.

Writing GXM directly rather than going through a GL layer helps here more than anywhere else in the
port. `sceGxmColorSurfaceInit` takes a pointer, so the surfaces can be initialised over one contiguous
block the port allocates itself: `sceGeEdramGetAddr()` can go on returning the base of that block, the
offsets keep meaning what they mean on the PSP, and the CPU readback in `RenderEngineSoft` stays an
ordinary read of memory the port owns rather than a texture-storage query and a sync. The same is not
true through vitaGL, where a texture's storage is wherever vitaGL put it.

What still needs care is that a GXM colour surface being written by the GPU is not safe to read or
sample until the scene using it has finished, so the ordering the PSP got for free from the GE's
in-order command queue has to become explicit synchronisation.

The cache-maintenance calls disappear in the process. On the PSP the GE reads the atlases straight out of
main memory, so the renderer writes the dirty lines back before drawing — and it already tracks
dirtiness per tile, so those `sceKernelDcacheWritebackRange` calls name exactly what changed. The Vita's
GPU cannot read an arbitrary `malloc`'d pointer, so atlas data has to be uploaded; the writeback ranges
are the natural signal for what to upload. That is why the compat layer's cache functions are no-ops
with a pointer to where the information went instead.

### The decision: a native GXM backend

Two ways were on the table:

1. **A `sceGu`-compatible shim over vitaGL.** The renderer sources keep calling `sceGu*` and
   `port/vita/gu` implements that surface in OpenGL 1.x. The PSP's fixed-function model maps onto GL1
   closely, all three renderer variants port at once, and `port/` stays one codebase for both consoles.
2. **A GXM backend written against the renderer's own interface.** Higher performance ceiling and full
   control of state and memory, which matters because the GE backend it replaces is hand-tuned.

**Option 2 was chosen.** The foundation built so far does not go to waste -- the toolchain, the
compatibility layer, the packaging path and the checks are all independent of which GPU API the
renderer uses -- but three things follow from the choice and should be settled before the renderer is
written.

- **Shaders are now the open problem.** GXM will not draw without compiled vertex and fragment programs,
  and `psp2cgc` is Sony's and not freely distributable. vitaGL sidestepped this by shipping precompiled
  `.gxp` blobs. Without it the options are: vendor precompiled `.gxp` blobs into the repository (they
  cannot then be changed by anyone without the proprietary compiler, which sits badly with a GPL-3.0
  project whose whole premise is that you build it yourself); require `libshacccg.suprx` on the console
  and compile at runtime through `SceShaccCg` (which `port/vita/shark_stub.c` was specifically written
  to avoid making players do); or find a third route. The shader set the compositor needs is small --
  textured with a palette, textured modulated by vertex colour, flat colour, and the 3D pass -- which
  makes vendoring blobs tractable, and makes the licensing question the deciding one rather than the
  engineering.
- **vitaGL stops being a dependency of the renderer**, but `scripts/deps-vita.sh` should not be removed
  yet: `tests/vita/vitagl_features.c` is currently how the port proves the GPU offers paletted textures,
  stencil and render-to-texture, and that check wants replacing with the GXM equivalents rather than
  simply deleting.
- **More has to be written by hand**: the GXM context, the shader patcher, memory pools, the render
  targets and the display queue are all things vitaGL would have supplied. The EDRAM region table below
  becomes simpler in this design rather than harder, because GXM colour surfaces can be initialised over
  memory the port allocates itself -- `sceGxmColorSurfaceInit` takes a pointer -- so the flat "EDRAM"
  block can be real memory again and `sceGeEdramGetAddr()` can keep meaning what it means on the PSP.
  That is a genuine advantage of this route over the shim and is worth exploiting.

## Audio

`port/native-audio-sound/sas_out.c` plays the DS sound engine's channels through the PSP's `sceSasCore`
voice mixer, so the CPU does no mixing. **The Vita has no `sceSasCore`.** The replacements are `sceNgs`,
which is the closest equivalent as a hardware-assisted voice graph, or a NEON software mixer feeding
`sceAudioOut`. Software mixing 16 DS channels is affordable on this CPU in a way it was not on the PSP,
and it avoids a second large unfamiliar API; the frame loop already paces `PSPNativeSoundAdvance` off
real elapsed time, which is the part that was subtle on the PSP and does not change.

## Build machinery

The app and renderer Makefiles include `$(PSPSDK)/lib/build.mak` and use `BUILD_PRX`,
`PSP_LARGE_MEMORY`, `EBOOT.PBP` and a custom `overlays/linkfile.prx`. VitaSDK has no `build.mak`, so the
link and packaging steps are new work: `-Wl,-q` plus `vita-elf-create`/`vita-make-fself`/`vita-pack-vpk`,
a replacement linker script for the overlay layout, and a VPK install path in place of
`scripts/install.sh`. `scripts/check_native_pbp.py` enforces the PSP firmware loader's 32 MiB section
bound and has no Vita counterpart — that check simply goes away, along with the reason SoulSilver splits
`.native.backing` at all.

## Small things found while surveying

- **Only three inline-assembly sites in the whole port**, all MIPS: `stackprobe.c` reads `$sp`, and the
  two copies of `os_sync.c` implement `DC_WaitWriteBufferEmpty` with the MIPS `sync` instruction. On ARM
  these are `mov %0, sp` and a data-memory barrier.
- Five `.S` files, which look like data tables rather than code.
- The DS is an ARM machine, so the decompilation's occasional ARM-flavoured assumptions are now closer
  to the target than they were on MIPS rather than further away.
- The PSP and Vita **button bits are identical** (`SCE_CTRL_CROSS` is `0x4000` on both), so the port's
  button tables need no translation at all.
- `sceKernelWaitThreadEnd` gained an exit-status argument on psp2, and `sceKernelCreateCallback` has a
  different signature entirely. Both are wrapped. The compat header self-test is what found them.

## Status

| Piece | State |
|---|---|
| Pinned VitaSDK toolchain | Done (`scripts/toolchain-vita.sh`) |
| vitaGL and math-neon, no `libshacccg.suprx` needed | Done (`scripts/deps-vita.sh`, `port/vita/shark_stub.c`) |
| Platform layer: arena, tick, interrupts, threads, alarms, touch, clock | Done, compile- and link-checked against libntr |
| VPK packaging path | Proven, not wired into a build |
| Renderer | Native GXM backend chosen; not written. Shader compilation is the open question above |
| Audio (`sceSasCore` replacement) | Not started |
| On-screen keyboard (`sceUtility` → `sceIme`) | Not started |
| MIPS inline assembly | Not started |
| Touch input | Done: `TP_*` reads the front panel through `sceTouch`, no cursor and no stylus mode |
| Screen layout | Constants in `port/vita/include/vitapoke.h`; the renderer has to honour them |
| Build and link machinery | Not started |
