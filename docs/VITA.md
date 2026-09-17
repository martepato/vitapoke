# Porting pspoke to the PS Vita

This is the working document for the Vita port: what has been decided, what has been proven, and what
is left. `NOTES.md` is the PSP equivalent and still applies to anything shared.

This is a native Vita application built with VitaSDK, for an ARM CPU with four cores and 512 MB of RAM.
It began as a port of pspoke, which targets the PSP's single MIPS core and 64 MB; that target has been
removed and the Vita is the only one.

## Why it is worth doing

The code this port started from is shaped by three PSP limits, and the Vita has none of them.

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

Speed is a genuine open question rather than an assumed win. The PSP build reached around 30 fps with a
hand-tuned GE backend; a Cortex-A9 at 444 MHz is far ahead of a 333 MHz Allegrex on the CPU side, but
the first Vita renderer will not be as tuned as that GE one was, and nothing here has run on hardware
yet.

## What is proven

Everything in this section was built and linked with the pinned toolchain.

- **The toolchain.** `scripts/toolchain.sh` downloads one immutable `vitasdk/autobuilds` snapshot,
  SHA-256 verified against that release's own `SHA256SUMS`, into `.cache/vitasdk` — the same shape as
  `scripts/toolchain.sh` does for PSPDEV. It deliberately does not use `vdpm` or
  `bootstrap-vitasdk.sh`: both resolve a channel through `vitasdk.org`, which makes the compiler you get
  depend on when you ran the build, and the point of pinning is that it does not.
- **The GPU dependencies.** `scripts/deps.sh` builds vitaGL and math-neon at pinned commits into
  the toolchain. vitaGL is not the renderer's target (see the decision below), but it is how the port
  currently proves the GPU offers the texture formats the DS compositor needs.
  `port/vita/shark_stub.c` answers its runtime-shader-compiler calls with "no compiler", so vitaGL
  links without vitaShaRK. The renderer will need `libshacccg.suprx` for its own shaders -- see the
  decision below -- but nothing in the checks does.
- **The platform layer.** `port/vita` implements the DS interfaces the game and libntr call -- `OS_*`,
  `TP_*`, `RTC_*`, the pad registers -- directly on psp2. See **Where the boundary is** below for why
  that, and not the PSPSDK, is the line. `tests/vita/ds_surface.c` calls every one of them and is
  compiled and linked by `tests/vita/run.sh`, which is what catches a psp2 or libntr rename after a
  toolchain bump.
- **Packaging.** `arm-vita-eabi-gcc -Wl,-q` → `vita-elf-create` → `vita-make-fself` → `vita-pack-vpk`
  produces a loadable VPK. Proven with the vitaGL feature check, not yet wired into a build.

- **The game's own code, compiled for ARM.** `./build.sh game` (scripts/game.sh) fetches the
  pinned decompilation, stages the tree for ARM, generates the decompilation's headers, compiles the DS
  SDK replacement, and then compiles **all 1016 of the game's C files: 1016 / 1016, no failures.** The
  output is real ARMv7 Thumb-2 objects. This was the largest unknown in the project and it is now
  answered: the decompiled game is portable. Six minutes from a clean tree, no ROM needed.

Run it with:

```sh
./build.sh setup     # pinned VitaSDK + vitaGL, about 100 MB, one time
./build.sh check     # the checks that need no ROM
./build.sh game      # the SDK and the game's own code, compiled for ARM
```

`check` fetches libntr (the DS SDK replacement) because the platform layer is compiled against its
headers.

The PSP build is **gone**. Its entry point, build scripts, PSPDEV toolchain download, EBOOT loader
audit, memory-stick installer, emulator probes and test suite have been removed, along with the
PSP-only variables and libraries in every component Makefile. There is one target and one way to build.
What remains of that lineage is the code being ported -- the sceGu renderer in `port/native-stack-render`
and the sceSasCore audio backend in `port/native-audio-sound` -- kept because they are the reference
the GXM and audio work is being ported *from*, not because anything builds them.

## What compiling the game for ARM turned up

Four things stood between the tree and an ARM compile. The first is by far the most important, and it
is the kind of problem that does not announce itself.

- **The ARM EABI makes enums as small as they fit, and the game assumes four bytes.** `enum { A, B }`
  is one byte under the EABI default, so *every struct containing an enum changes size* -- silently,
  with no diagnostic. libntr catches a little of it by asserting struct sizes (that is how it surfaced:
  `sizeof(CARDiCommandArg)` came out 92 where the SDK requires a multiple of 32), but most of it would
  simply have been wrong. The 512 KB save file has a fixed layout that enum-bearing structs are written
  into, so the failure mode was saves silently incompatible with the DS. **`-fno-short-enums` is
  load-bearing** and is set for the whole Vita build in `scripts/stage.sh`.

  It has a visible consequence at the link: VitaSDK's own libraries are built with the EABI default,
  so ld reports every object of ours as *"uses 32-bit enums yet the output is to use variable-size
  enums"*. Those warnings are expected and are not worth silencing, because the thing they warn about
  -- an enum passed across that boundary -- does not happen: nothing this port calls in the C library,
  libstdc++ or vitaGL takes or returns an enum type (GL's `GLenum` is a typedef of `unsigned int`, and
  the psp2 stubs are assembly). Silencing them with `--no-warn-mismatch` would also hide a real
  mismatch between our own objects, which is the case that would matter.
- **libntr wants SDL.** On any target that is not `SDK_BUILD_ARM` its OS headers declare mutexes,
  alarms and threads in terms of SDL types. That is libntr's property, not the PSP's. VitaSDK ships no
  SDL, so `port/vita/sdl2-shim` declares the handful of names libntr refers to and `scripts/deps.sh`
  installs them where the toolchain looks. Some of those declarations are deliberately never
  implemented: they belong to libntr modules (`os_thread.c`, `os_mutex.c`, `os_alarm.c`, `os_tick.c`,
  `os_message.c`, `fs_file.c`) that this port replaces and that `filter-sdk.py` drops before the link.
- **Two header assumptions that PSPDEV happened to satisfy.** libntr's `nitro/card/backup.h` calls
  `malloc` and `free` without declaring them, and `fs_file.c` uses `tolower` the same way, both relying
  on the including translation unit. `stdlib.h` and `ctype.h` are force-included for the Vita build.
- **GCC 15 promoted some old-C diagnostics to errors.** This is decompiled code full of casts the
  original compiler accepted, so `-Wno-incompatible-pointer-types` puts one of them back to a warning
  -- the same flag the SoulSilver half of the tree already passed.

Two smaller things, both in `port/` rather than upstream:

- pspoke's own patch to the decompilation widened seven `#ifdef SDK_BUILD_ARM` conditionals with a
  console name, where what they actually ask is whether pointers are 32 bits. They now say
  `#if __SIZEOF_POINTER__ == 4`, which is the real question, comes from the compiler, and cannot drift
  as targets are added.
- The decompilation's `src/port/gui_*.c` is the PC port's debug GUI -- Dear ImGui over SDL with a GL
  context, for a cheat menu and map jump. It is not game code and is never linked into a console build,
  so the Vita build does not compile it. It was also the only thing in the tree that wanted real SDL.

What is left before anything runs: the renderer, the audio backend, and the link step. The overlay
layout in particular needs a linker script of its own -- the PSP build edits PSPSDK's PRX script, which
has no Vita counterpart -- and the per-overlay ROM bounds table needs your ROM.

## Verifying against emulated hardware

`tests/vita/vita3k.sh` (or `./build.sh emu-check`) builds `tests/vita/ds_runtime.c` into a VPK,
boots it in the [Vita3K](https://vita3k.org) emulator and reads back the report the test writes to
`ux0:data`. It downloads the emulator itself (about 65 MB) and takes a couple of minutes, which is why
it is separate from `./build.sh check`.

This is the difference between believing the platform layer works and knowing it. 27 checks run, and
the ones that matter are the ones a compiler cannot reach:

- **The DS execution lock does what it claims.** Two DS threads busy-loop while checking that nothing
  else has taken over. The same two threads run first as plain psp2 threads with no lock, as a control:
  **248 interleavings without the lock, 0 with it**. That number is the whole argument for the lock in
  one line -- the race is real on this hardware, not theoretical, and the lock is what stops it. A test
  that only reported 0 would prove nothing, because a machine that never interleaves scores 0 too.
- **A wake is not lost** between a thread queueing itself and sleeping.
- **Alarms** fire once, repeat when periodic, stay cancelled, and report `OS_PROCMODE_IRQ` inside the
  handler.
- **The psp2 semantics the design rests on**: an `SCE_KERNEL_MUTEX_ATTR_RECURSIVE` LwMutex really is
  recursive, and `SCE_EVENT_WAITCLEAR` really does return immediately on an already-set flag and
  consume it. Both were assumptions until this ran.

Writing these found one real bug in the lock (`VitaOS_Release` returned a wrapped count) and one
missing guard (`VitaOS_Leave` unlocked the mutex even when the caller did not hold it, which would have
let two DS threads into DS code at once -- silently, and only sometimes).

What it does not prove: Vita3K is not a console. Timing, thread scheduling and the touch panel are all
approximations, and the renderer is not exercised at all yet. Anything that depends on real timing
still needs hardware.

Notes for anyone reproducing it: Vita3K has no tagged releases, only a rolling `continuous` build, so
unlike the toolchain this cannot be pinned. It also refuses to run as root, needs a display (Xvfb is
fine) and an OpenGL driver (llvmpipe is fine, since nothing here draws), and its AppImage expects
`libOpenGL.so.0` and `libEGL.so.1` from the system. The script handles or reports each of those.

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

### The decision: compose in software, and go through vitaGL

Three ways were on the table:

1. **A `sceGu`-compatible shim over vitaGL.** The renderer sources keep calling `sceGu*` and the port
   implements that surface in OpenGL 1.x. Rejected: it makes the Vita port a PSP emulation layer
   rather than a Vita port, which is not what this project is for.
2. **A GXM backend written against the renderer's own interface**, reproducing on the Vita's GPU what
   the PSP build did on the GE: DS tile atlases as paletted textures, windows and priorities in the
   stencil buffer, the 3D buffer composited as a texture.
3. **Compose the DS's 2D in software and ask the GPU only for what is genuinely triangles**, which is
   the DS's 3D output and putting the finished screens on the display.

**Option 3 is what is written**, and options 2 and 3 are not as far apart as they look: the PSP build
already had the software compositor, because there were DS features its GE backend could not express
(affine and bitmap backgrounds, windows, VRAM display mode) and it fell back to software for any frame
that used one. What changes on the Vita is that the fallback is affordable as the main path: a 444 MHz
Cortex-A9 with NEON against a 333 MHz MIPS core, and three other cores besides. And it is not an
approximation. The DS's 2D pipeline is per-pixel state -- priorities, windows, the two blending units,
the master brightness -- and melonDS's software renderer is the reference for what that hardware
actually does, where a GPU version of it is a construction that is nearly right.

So the GPU's work is: two textures drawn as quads, and rasterising the 3D triangle list that libntr's
geometry simulator produces. `port/native-vita-render/gpu.h` is that boundary, eight calls wide.

**Not GXM directly, and the shader question is not what it first looked like.** GXM needs compiled
vertex and fragment programs, `psp2cgc` is Sony's and not freely distributable, and the only compiler
available to a homebrew toolchain is `SceShaccCg` -- `libshacccg.suprx`, extracted from a firmware
update. The first version of this note claimed vitaGL avoided that by carrying precompiled shaders
for its fixed-function pipeline. **That was wrong**, and the way it was found out is instructive: the
port reached the game's first frame and hung, and vitaGL's last act in the log was opening
`ux0:data/shader_cache/v31/v/00000028-0.gxp`. vitaGL *writes* its fixed-function shaders as Cg source
and compiles them on the console through vitaShaRK, caching each variant in that directory. So the
compiler is needed either way, and `port/vita/shark_stub.c` -- which answered those calls with "no
compiler" -- was answering a question that had to be answered properly instead.

So the renderer needs `libshacccg.suprx` present at runtime. That is a real cost and worth stating
plainly: it is on most custom-firmware consoles but it is not on a stock one, and it is not in the
Vita3K emulator unless the user puts it there. `port/native-vita-render/gpu.cpp` brings the compiler
up itself at startup, before anything is drawn, so that a console without it gets one line saying
exactly what is missing rather than a crash inside vitaGL's first draw.

What vitaGL still saves, and why it is still the right choice: it writes those shaders, and it owns
the GXM context, the shader patcher, the memory pools, the render targets and the display queue --
all of which a GXM renderer would have to have written, for the same runtime dependency. It is a
thin layer over GXM, `gpu.cpp` is the only file in the port that includes a GL header, and rewriting
it against GXM later is a contained change.

This is a decision about where the renderer starts, not a ceiling. vitaGL is a thin layer over GXM;
`gpu.cpp` is the only file in the port that includes a GL header, and rewriting it against GXM is a
contained change if profiling on hardware asks for one. What it costs today is the shader-level
control GXM would give -- the DS's paletted textures go to the GPU expanded to 32-bit rather than as
P4/P8 with a palette, which costs memory and a decode rather than correctness.

**Testing without the compiler.** Everything on the DS side of this renderer -- both 2D engines, the
frame, the pacing, the game -- needs no GPU at all, and that is the part most likely to be wrong. So
the renderer has a switch, `NO_GPU=1`, that composes without drawing, and `FRAME_DUMP=<n>` writes
what was composed to the memory card for `tests/vita/raw_to_png.py`. That pair is how the 2D output
below was checked against the real game on a machine with no shader compiler in it. A build for
playing uses neither.

### What the 3D path does, and what is unverified

libntr's simulator already decodes the DS's geometry commands and does the transform, lighting and
clipping on the CPU. What reaches `port/native-vita-render/g3_backend.cpp` is a triangle list in DS
screen coordinates with texture coordinates in texels, a colour per vertex and a depth. It is drawn
into a 256x192 render target and read back into the buffer the 2D compositor blends as background
layer 0.

The readback is the cost: it makes the CPU wait for the GPU, once on each frame that shows the 3D
layer -- and it is skipped entirely on the frames that do not, which is most of them outside the field
and battle. The PSP paid almost nothing for the same step because its GE drew into memory the CPU
could simply read. Keeping it is what makes the DS's layer ordering, windows and blending come out
right; compositing the 3D layer on the GPU instead means moving the whole 2D pipeline there with it.

None of this has met a real game frame yet. Two things to check first when it does: the depth
convention (the DS's far plane is +1 and `glOrtho` with a near of -1 maps an eye z of -1 to the far
plane, so the depth is handed over negated -- see `G3SIM_AddVtx`), and the texture cache, which has
sixteen slots and a megabyte and no eviction, so a scene that needs more says so in the log rather
than silently drawing the wrong thing.

## Audio

`sceSasCore`, the PSP's hardware voice mixer, is what the PSP build handed the DS's 16 channels to, so
its CPU did no mixing at all. **The Vita has no `sceSasCore`.** `sceNgs` is the nearest equivalent as a
hardware-assisted voice graph, but the DS's own mixer is already in this tree -- libntr's simulator
mixes the 16 channels, ADPCM and PSG included, and the PSP build compiled it and then used it only in
its silent mode.

So the Vita uses it for real. `port/vita/audio_out.c` opens a `sceAudioOut` port at 16000 Hz and plays
what the mixer produces, on its own thread above the game's priority. The DS mixes at 15989.5 Hz
(one sample every 1048 ticks of its sound clock), so the console's audio hardware does the conversion
to its own rate and nothing is resampled in software; the pitch error is -0.066%, about a hundredth of
a semitone, and is the same error the PSP build had.

Sound is **on by default**, which the PSP build could not afford: mixing cost it about five frames a
second on one 333 MHz core. Here the mixer runs on one of four 444 MHz cores while the game runs on
another. NEON is the obvious next step for the mixer if it turns out to matter, and has not been
needed yet because nothing has been measured on hardware.

The pacing is unchanged and worth keeping: `VitaNativeSoundAdvance` is called once per game frame with
how long that frame really took, not with a nominal 1/30 s, because a sound engine advanced by a
nominal frame drifts against the audio hardware until the buffer starves.

## The link, and the overlay modules

The DS does not run the game as one program. It runs a small always-resident part plus about a hundred
code overlays that the SDK loads over each other from the cartridge as the game moves between scenes.
The port links all of it at once, which is fine for code on a console with memory to spare -- but not
for data. Two overlays that were never in memory together may have static variables the game expects
to be freshly initialised when its scene starts, and on the DS that came for free, because loading the
overlay copied its data section out of the ROM again.

So each overlay's data, BSS and static constructors go into their own output sections, and
`overlays/overlay.c` re-initialises a module's sections when the game asks for it to be loaded.
`overlays/gen-link.py` writes those sections out twice: as a linker script and as a table of bounds
(`ranges.h`) that the loader reads.

The linker script is where the two consoles differ most. The PSP build had to edit PSPSDK's PRX script
by hand, because its layout needed changes in the middle of it. On the Vita the sections are added to
the toolchain's own script with GNU ld's `INSERT AFTER .data`, so there is no base script to maintain
at all -- `-Wl,-T,overlays/overlays.ld` supplements rather than replaces. One thing does matter in the
ordering: all the loadable sections come first and all the BSS after, because a module's zero-filled
section between two modules' data would split the loadable segment.

**The build needs no ROM.** The one thing the port wanted from it at build time was each module's DS
load address and size, which the game reads back as metadata and never dereferences. That comes out of
the ROM's own overlay table, and `overlays/overlay.c` now reads it at startup instead -- so one build
works with any dump of the same game, and a ROM that is not this game or not this region is caught at
startup by its table having the wrong number of modules, with a line in the log saying so.

What the PSP needed that the Vita does not:

- **`sdk-g3stack`**, which rebuilt four SDK objects so the geometry engine's command lists could live
  on the stack and be borrowed by the GE renderer. Nothing borrows command lists here.
- **`opttex_redirect.py`**, which redirected VRAM writes so the GE renderer could tell which tile
  atlases had changed. The software compositor reads DS VRAM directly.
- **`spl-divzero`**, which guarded ten divisions in the particle engine because PSP GCC traps on a zero
  divisor where the DS's ARM returns. This console is the one the guards were unnecessary on.

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

## Booting it, and running it

`./build.sh boot` installs the built VPK into Vita3K, runs it, and prints what the port wrote to
`ux0:data/vitapoke/log.txt`. It is the first thing to run after a build, and it is how everything
below was found. With `--rom your-dump.nds` it uses a real ROM; without one,
`tests/vita/make_probe_rom.py` writes a file shaped like a DS ROM -- a header, an overlay table, empty
file tables, no game data -- which is enough to test everything up to the game's first data read.

**With a real ROM, the game runs.** In Vita3K, built with `NO_GPU=1 FRAME_DUMP=20` (see the renderer
section for why), it gets through its own startup, loads its overlays, and plays the opening: the
copyright screen, the GAME FREAK logo, the Pokémon logo, in colour, both screens, composed by this
port's software 2D renderer from the real ROM. About a thousand frames were run and dumped to check
it. That is the answer to the question the whole port exists to ask.

```
[APP] vitapoke starting: data in ux0:data/vitapoke
[RENDER] ready: software 2D, two 256x192 panels at 2x on a 960x544 display
[APP] entering NitroMain
[STARTUP] OS arena initialised, 6 MiB in main backing
[AUDIO] output ready: 16000 Hz, 256-sample buffers, 8192-sample ring
[AUDIO] init output=ready sound=on
[OVERLAY] load id=77
[MEM] enter gOpeningCutsceneAppTemplate frame=0 ... files_open=5/5 open_fail=0 read_fail=0
[FRAME] 1 game_us=0 audio_us=2635 render_us=8949 bind_us=3277 compose_us=2966 present_us=105
[FRAME] 10 game_us=272599 audio_us=10791 render_us=39831 bind_us=22 compose_us=9045 present_us=1
```

So: the module loads, the platform layer comes up, the DS arena, the tick clock, the interrupt table
and the vertical blank are in place, the ROM's overlay table matches what the build expects, the file
system serves the game's files, the heaps and task managers are built, the sound engine's output port
opens, the overlay loader re-initialises a module's data, and the game's own scenes run.

What the numbers say, remembering that they are an emulator's: about 30 ms of game code per frame and
under 10 ms of compositing, with the frame caches skipping most frames' 2D work entirely (`compose_us`
is 1 µs on the frames they hit). The emulator itself runs at roughly a sixth of real time, so nothing
here is a frame rate for the console -- only hardware can give that.

**What has still never run:** the GPU. `NO_GPU=1` is how the above was checked, because Vita3K has no
shader compiler unless the user puts `libshacccg.suprx` into it, and without a compiler vitaGL cannot
make its fixed-function shaders (see the renderer section). So the panel present, the 3D rasteriser
and the audio actually reaching a speaker are all still unverified. Nor has any button been pressed:
nothing here drives the pad, so the game plays its opening and waits.

Two things to look at first when it does run on a console: the dark red bands at the outer edges of
both panels in some scenes, which may be the cutscene's own backdrop or a background's horizontal
wrap, and the 3D depth convention in `G3SIM_AddVtx`.

### Six things that cost an afternoon each

Worth writing down, because none of them is discoverable by reading:

- **`vglInit` does not return success.** Despite the name and the `GLboolean`, vitaGL returns
  `GL_TRUE` only when the display size it was asked for did not fit and it fell back to a smaller
  one. A working init returns `GL_FALSE`. Checking it the obvious way makes the renderer refuse to
  start on a perfectly good GPU.
- **`abort()` does not abort.** newlib's raises `SIGABRT`, psp2 has no signals, and the raise lands
  in an unimplemented `kill` stub: the console dies on an undefined instruction with nothing written
  down. That is the worst possible failure for this port, because `abort` is where the SDK's
  assertions and a dozen of the port's own impossible cases end up. `port/vita/memlog.c` replaces it
  with one that logs and exits, and the SDK's `OS_Terminate` and the game's `GF_ASSERT` log their
  caller's address, because a release build compiles their messages out.
- **`vita-elf-create` needs room after the code.** It appends about 3 KB of SCE metadata to the end
  of the first loadable segment; how much room there is depends on where the code happens to end
  inside its last page, and this executable's ended 592 bytes short. Padding does not help by itself
  -- the next segment just moves to the page after. `overlays/gen-link.py` makes the read-only
  segment end at a *known* offset into a page instead.
- **The DMA guard was a PSP memory map.** `MI_SendGXCommand` and friends validated their pointers
  against the PSP's user RAM, VRAM and scratchpad ranges, and refused everything on this console,
  where memory is wherever the allocator put it. The check that still means something -- a pointer
  that cannot be valid at all -- is what is left.
- **vitaGL waits for vertical blank by default.** Its present does, unless told otherwise. The game
  already waits for one twice per update -- that is its clock -- so a third wait would have run it at
  20 frames a second rather than 30, with nothing about the frame looking wrong. `vglWaitVblankStart(GL_FALSE)`
  in `VitaGpuInit` is what keeps the game's own pacing the only pacing.
- **The game checks the cartridge's maker code.** `CheckForMemoryTampering` calls `OS_Terminate` if
  the header does not say Nintendo, so the port has to put the ROM's real header where the game
  looks for it (`CARD_Init` does), and the probe ROM has to carry those two bytes to get past
  startup at all.

Vita3K is also not a console: its timing, scheduling and GPU are approximations and its touch panel
is a mouse. A clean boot there is not proof that a Vita boots it. What it does give is a boot at all,
repeatably, in the project's own build.

## Status

| Piece | State |
|---|---|
| Pinned VitaSDK toolchain | Done (`scripts/toolchain.sh`) |
| vitaGL and math-neon, no `libshacccg.suprx` needed | Done (`scripts/deps.sh`, `port/vita/shark_stub.c`) |
| Platform layer: arena, tick, interrupts, threads, alarms, touch, clock | Done; 27 runtime checks pass in Vita3K |
| Build machinery: one tree, placeholders for the target's flags | Done (`scripts/stage.sh`, `port/build/vita.mak`) |
| DS SDK replacement compiled for ARM | Done: 270/271, and the one failure is a module the filter drops |
| The game's own 1016 C files compiled for ARM | Done: 1016/1016 |
| DS message queues, mutexes, cache maintenance | Done (`port/vita/os_sync.c`), with the execution lock released across every blocking wait |
| Frame driver, entry point, log, save file, owner profile | Done (`port/vita/frame.c`, `app_main.c`, `memlog.c`, `backup.c`, `owner_info.c`) |
| Vertical blank | Done: the console's own, through `sceDisplayWaitVblankStart` (`port/vita/cadence.c`) |
| Renderer: DS 2D composed in software, put on the display through vitaGL | Written (`port/native-vita-render/`); no real game frame has reached it |
| Renderer: DS 3D rasterised on the GPU | Written; geometry and depth conventions unverified |
| Audio: the DS mixer's output through `sceAudioOut` | Written (`port/vita/audio_out.c`); not yet heard |
| Overlay data layout and the link | Done (`overlays/gen-link.py`, `INSERT AFTER .data`) |
| VPK packaging | Done, and wired into `./build.sh game` |
| No ROM needed to build | Done: the overlay table is read from the ROM at startup |
| Screen layout: both DS screens at 2x, side by side | Done (`port/vita/include/vitapoke.h`, honoured by the renderer) |
| Touch input | Done: `TP_*` reads the front panel through `sceTouch`, no cursor and no stylus mode |
| On-screen keyboard | Not needed: the game's own naming screen is used, because the Vita has a touchscreen |
| MIPS inline assembly | Done: `mov %0, sp` and `dmb ish`, guarded on `__arm__` |
| Runtime checks in the Vita3K emulator | Done (`./build.sh emu-check`) |
| Does it boot? | Yes, in Vita3K, from a real ROM: through the game's own startup and into its opening |
| Does the DS 2D renderer work? | Yes: the copyright screen, the GAME FREAK logo and the Pokémon logo, in colour, both screens |
| Does the GPU path work? | **Unknown.** It needs `libshacccg.suprx`, which the emulator here does not have |
| Does it play? | **Unknown.** No button has been pressed, no sound heard, no 3D drawn |
| SoulSilver | Not started: its sources are here, only Platinum has a build driver |
