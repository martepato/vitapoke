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

  The download is the default, not the only way. Set `VITASDK` — the variable VitaSDK's own
  instructions ask you to export — and every part of the build uses that install instead; it is also
  the answer on a platform the snapshot does not cover, and `scripts/common.sh` decides once which
  of the two is in play and passes it down, so a child step cannot mistake one for the other. What
  `./build.sh setup` then writes into somebody's own install, and how to tell it not to, is in
  README.md under "Using a VitaSDK you already have". Two things make switching safe rather than
  merely possible: `scripts/deps.sh` asks before writing anything into an install it did not create,
  and `scripts/game.sh` records the toolchain's identity beside its build stamps and throws the
  compiled tree away when that changes — otherwise a build that started under one compiler would
  quietly link objects from both.
- **The GPU dependencies.** `scripts/deps.sh` builds vitaGL, vitaShaRK and math-neon at pinned
  commits into the toolchain. vitaGL is how the port puts the composed DS screens on the display, and
  vitaShaRK is how vitaGL compiles the shaders it writes for its own fixed-function pipeline: there is
  no precompiled path, so the renderer needs `libshacccg.suprx` on the console at runtime. See the
  decision below for what that costs and why it is accepted. Building vitaShaRK needs one header
  VitaSDK does not ship, `shacccg_ext.h`; `port/vita/shacccg_ext.h` is that declaration and
  `shacccg_ext_stub.c` defines the two functions, because there is no import stub for them either.
  The port never calls them -- it initialises the compiler through `shark_init_simple`.
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
compiler is needed either way, and the stub that had been answering those calls with "no compiler"
was answering a question that had to be answered properly instead. It is gone; `scripts/deps.sh`
builds the real vitaShaRK.

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

The front half of this has met the real game: the opening cutscene drives the geometry engine, the
port turns its commands into triangles (18 to 162 polygons a frame, climbing through the scene) and
the 2D engine asks for the layer to be composited in. What has not happened is the drawing, because
that is the part behind the shader compiler. Two things to check first when it does: the depth
convention (the DS's far plane is +1 and `glOrtho` with a near of -1 maps an eye z of -1 to the far
plane, so the depth is handed over negated -- see `G3SIM_AddVtx`), and the texture cache.

Both have since run against the real game. The cache is 256 slots and 8 MB with least-recently-used
eviction, because sixteen slots was the binding constraint rather than the byte budget: the title
screen filled it at 76 KB and then drew untextured for eighty thousand frames. The game asks for far
more texture *binds* than distinct textures -- 26 binds a frame against 147 distinct textures in a
field scene, 92% of them hits -- so the cache being a cache, rather than a decode per bind, matters.

The other thing the real game settled is where texture memory *is*. The geometry engine addresses
textures in a flat 512 KB space and their palettes in a flat 96 KB one, and which VRAM bank serves
which part of each is whatever the game last wrote to VRAMCNT. This port read the image from bank A
and the palette from bank E and asked no questions, which is right for the opening and the title
screen and wrong for most of the rest of the game: Platinum puts texture palettes in F and G at 43 of
the 67 places it sets them up, and textures in B or C at 23. The overworld drew black because of it --
palette read out of bank E, which in the field is engine A's sprite graphics, so every texel came out
colour 0, and the only textures that still appeared were the direct-colour ones that have no palette.
Both spaces are now resolved a slot at a time from the registers (`texImageSlot`, `texPlttSlot`,
`texRange` in `g3_backend.cpp`), and a slot no bank serves reads as nothing rather than as somebody
else's memory. The log names the banks in use whenever they change.

## The game's data: unpacked at build time, not read from a ROM

The game asks its file system for data by name and only by name. Eleven direct `FS_OpenFile` calls,
and all 2621 `NARC_*` calls in the game through one table of paths in `narc.c` -- no file ids, no
directory walks, no raw ranges. That one fact decides the design: if the data is files with those
names, nothing else about the game has to change.

So `./build.sh game` unpacks it. `scripts/extract_assets.py` reads a ROM's own file system and
writes out its 340 files at their own paths (95.5 MB for Platinum), plus `nitrofs.idx`, 14 KB
holding the three things the port needs from a ROM that are not files in it: the 512-byte cartridge
header (`CheckForMemoryTampering` reads the maker code, and `CARD_Init` copies the header into the
hardware buffers the game looks in), the ARM9 overlay table (122 modules of metadata the game reads
back but never dereferences), and every file's path by file id. Those go into the VPK beside the
executable, and the port opens them from `app0:`. None of the ROM's code is carried: every overlay's
code is already compiled into the executable, which is what the rest of this document is about.

A ROM at `roms/Platinum.nds` is used automatically, `--rom FILE` points elsewhere, and **a build
with no ROM still works** -- the VPK then has no game data in it and reads one from
`ux0:data/vitapoke` at run time, which is what this port did before and what keeps a clone
buildable, and the checks runnable, by somebody who has no dump. `port/native-audio-app/services/romfs.c`
serves both and says which in the log.

### Why this is not simply the ROM in the VPK

Because the ROM carries a second copy of the code. The executable already contains every overlay's
ARM code, compiled for this console; the ROM's copy would be 30 MB of ARM946 instructions that
nothing will ever execute. Unpacking drops them, and a 53 MB VPK is the result rather than a 128 MB
one. It is also the honest shape: the port is a native program with its data beside it, not an
emulator with an image.

### The handle pool, and the bug it exists to avoid

The version of `romfs.c` this replaces served every DS file from **one** open handle, and the
comment above it records why: a console has a small, hard limit on how many files may be open on its
memory card at once, and past it `fopen` fails and the caller gets a file it believes is valid. The
PSP build found that on hardware, in the field map, which opens more archives at once than any other
scene. One handle, and every `FSFile` seeking before it read, was how that was avoided.

Bundled files cannot share a handle, because they are different files. So the bound is kept
deliberately instead: at most eight host handles are open whatever the game does, keyed by file id,
the least recently read is closed to make room, and every read still seeks first because the handle
under it may have been reopened for somebody else since. The 512 KB absolute-offset block cache that
the ROM path uses is not needed here -- it existed to make the repeated 2-4 byte NARC header reads
cheap, and a per-file handle with newlib's own buffer in front of it does that.

The counters are in the `[MEM]` line so this is measured rather than assumed. Through startup, the
opening, the title screen and into a new game -- 10,200 frames:

```
files_open=5/7 open_fail=0 read_fail=0 handles=8 evicted=13 handle_fail=0
```

Eight handles held, thirteen evictions in five and a half minutes, and nothing ever failed to open.
The game's working set is a little above eight at a scene change and flat otherwise. `evicted`
climbing steeply in the field map would be the sign to raise the pool; `handle_fail` above zero
would mean the console's limit was hit anyway, which is the failure this is shaped to prevent.

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
`--press 120:Return,150:x` works the buttons on a schedule, which is what turns a boot into something
closer to a play session; `--shacccg FILE` installs the shader compiler the renderer needs.

**With a real ROM, the game starts.** Pressing START at the title screen and A on the menu takes it
through `gMainMenuAppTemplate`, `gGameStartRowanIntroAppTemplate` and into
`gRowanIntroAppTemplate` -- Professor Rowan's introduction, the first scene of a new game -- and it
sat there running at 30 frames a second for another four minutes, sixteen sound channels playing,
with no assertion, no fatal, and a flat heap. `tests/vita/boot.sh --press 200:c,220:Return,...` is
the whole of it.

```
[MEM] enter gTitleScreenAppTemplate      frame=6420
[MEM] enter app@0x813be2d4               frame=6691   <- START
[MEM] enter gMainMenuAppTemplate         frame=6697
[MEM] enter gGameStartRowanIntroAppTemplate frame=6710 <- A on "New Game"
[MEM] enter gRowanIntroAppTemplate       frame=6713
[PERF] frames=14400 fps=30.01 game_us=32427 idle_us=32373 render_us=785 polygons=0
[AUDIO] out=1 fill=1600 written=7864464 underruns=2361 dropped=0 peak=23116 active=ffff
```

**Left to itself it plays the whole attract sequence.** In Vita3K, built with `NO_GPU=1`
(see the renderer section for why), it gets through its own startup, loads its overlays, plays the
opening cutscene -- the copyright screen, the GAME FREAK logo, the Pokémon logo, in colour, on both
screens, composed by this port's software 2D renderer from the real ROM -- reaches the **title
screen**, and loops back to the cutscene the way the real game does when nobody presses anything.
Nine thousand frames, five minutes, no assertion, no fatal, no leak: the heap sits at 17 MB and does
not climb between loops.

```
[APP] vitapoke starting: data in ux0:data/vitapoke
[GPU] built with VITAPOKE_NO_GPU: the DS screens are composed and not drawn
[RENDER] ready: software 2D, two 256x192 panels at 2x on a 960x544 display
[APP] entering NitroMain
[STARTUP] OS arena initialised, 6 MiB in main backing
[AUDIO] output ready: 16000 Hz, 256-sample buffers, 8192-sample ring
[OVERLAY] load id=77
[MEM] enter gOpeningCutsceneAppTemplate frame=0 ... files_open=5/5 open_fail=0 read_fail=0
[FRAME] 1 game_us=0 audio_us=2504 render_us=5261 bind_us=2526 compose_us=2321 present_us=109 polygons=0
[G3] first 3D frame: 2 polygons, layer shown
[MEM] enter gTitleScreenAppTemplate frame=6420 ... heap_used=13327944 heap_high=13329160
[PERF] frames=6900 fps=29.99 game_us=29821 idle_us=28905 render_us=3387 compose_us=4465 polygons=996
[MEM] enter gOpeningCutsceneAppTemplate frame=7972 ... heap_used=17232008
[PERF] frames=9000 fps=29.99 game_us=29363 idle_us=27305 render_us=3851 compose_us=5141 polygons=3488
[AUDIO] out=1 fill=1269 written=501364 underruns=23 dropped=0 peak=21912 clips=0 active=7fff
[INPUT] first button press: pad=0x00000008 ds=0x008
```

So: the module loads, the platform layer comes up, the DS arena, the tick clock, the interrupt table
and the vertical blank are in place, the ROM's overlay table matches what the build expects, the file
system serves the game's files, the heaps and task managers are built, the overlay loader
re-initialises a module's data, and the game's own scenes run and hand over to each other. And five
things that only a running game could show:

- **It holds 30 frames a second in the emulator.** Nine thousand frames in five minutes of wall
  clock, `fps` reading 29.99-30.01 the whole way: the game's own rate, on an emulator, on an
  ordinary Linux host. That is not a number for the console -- Vita3K's timing and scheduling are
  approximations and its CPU is not a Cortex-A9 -- but a port that could not keep up would not read
  30 here either. About 30 ms a frame is the game's own code and 2-6 ms is the software compositor.
- **The 3D path is fed.** `[G3] first 3D frame` is the first frame the geometry engine produced
  anything on; from there the count per frame climbs through the opening and reaches 3488 in the
  title sequence, with the 2D engine asking for the 3D layer to be composited in. Every DS geometry
  command the game issues reaches the port's `G3SIM_*` backend and becomes a triangle list. Whether
  those triangles come out *right* is still unknown, because nothing rasterised them.
- **The sound engine produces sound.** Half a million samples accepted by `sceAudioOut` with
  `peak=21912` of 32767 and fifteen channels playing: the DS mixer is synthesising real audio from
  the ROM's sequences, not silence. The underrun count stops climbing after the first seconds.
- **Buttons do what they should.** `pad=0x00000008` is `SCE_CTRL_START` and `ds=0x008` is the DS's
  START bit, so the mapping in `port/vita/input.c` lands where the game reads it -- and the game acts
  on it: the presses above walk it from the title screen into a new game.
- **The heap is stable.** 17 MB in use at the second loop, `heap_high` equal to `heap_used`, no file
  open or read failing, the stack 8 KB into its megabyte.

**And the GPU path runs too.** With `libshacccg.suprx` in the emulator (from
[AnimMouse/SceShaccCg](https://github.com/AnimMouse/SceShaccCg), which is the module as a firmware
update carries it -- `SCE\0` header, SHA1 `12893f60...`), the whole renderer works: vitaGL brings up
vitaShaRK, compiles its fixed-function shaders through the real compiler -- about 0.9 s on the first
present, once, cached afterwards -- and from then on the two composed DS screens are textured quads
on the GPU and the DS's 3D is rasterised there.

```
[RENDER] ready: software 2D, two 256x192 panels drawn 480x360 side by side on a 960x544 display
[FRAME] 1 ... render_us=926781 present_us=922170        <- the first present compiles the shaders
[FRAME] 3 ... render_us=1118586 present_us=254          <- and never again
[PERF] frames=7800 fps=29.63 render_us=9856 compose_us=6715 present_us=330 polygons=996
       tex=123/1346816 binds=114083 hits=111874 decodes=123 evictions=0
```

7800 frames at 22-30 fps under a software OpenGL driver, present costing 300-500 µs, no assertion,
no fatal. The texture cache holds 123 textures in 1.3 MB with a 98% hit rate and has never had to
evict one.

**And it looks right.** `tests/vita/boot.sh --shot 250:title.png` captures the X root window, which
under Xvfb is exactly the Vita's 960x544 display, so the PNG is the frame a player would be looking
at -- the only way to answer "does it look right" without a console. The captures show the opening
cutscene's city skyline with Lucas running past it on one screen and Dawn past a town on the other,
the title screen's starter banner, and Professor Rowan standing over his dialogue box reading
"However, everyone just calls me the Pokémon Professor." Correct sprites, correct colours, correct
font, both panels. The captures are not committed: they are the game's own artwork, and nothing of
the game belongs in this repository.

Two bugs only a drawn frame could have found, both fixed here:

- **The panel layout put one screen on top of the other.** Two screens at an integer 2x are
  512x384 each, and 1024 columns do not fit in 960: the touch screen was drawn over the main
  screen's right-hand 64 columns, which is where the main screen's own interface often is. The
  layout note claimed the overlap was "taken off the outer edges", which nothing did. Each panel is
  now 480x360 -- half the width, and exactly the DS's 4:3 -- so both screens are whole and neither
  covers the other. The cost is a 15/8 scale instead of 2x, which is why the panels are drawn with
  bilinear filtering, and why their texture coordinates stop half a texel inside the used area:
  without that, filtering pulls in the unused part of the 256x256 panel texture and leaves a
  one-pixel seam down the edge of each screen.
- **The texture cache filled and stopped admitting anything.** Sixteen slots, inherited from the
  PSP build, and no eviction: the cache filled during the opening cutscene at 76 KB -- nowhere near
  its byte budget, so the *slot count* was the constraint -- and every texture the title screen
  then wanted was refused. It drew untextured for eighty thousand frames, and said so eighty
  thousand times. It now has 256 slots, an 8 MB budget (more than the DS's 512 KB of texture VRAM
  can expand to however it is sliced) and least-recently-used eviction, and the `[PERF]` line
  carries an eviction count so a scene that needs more than it holds is visible rather than silent.

One thing worth a second opinion from someone with a DS: on the title screen the starter banner
shows as black silhouettes on one panel while the other shows it in colour, and which panel it is
alternates frame to frame. That pattern is an animation, not a stuck blend -- a bug would sit on one
screen -- and it matches the reveal effect the title screen does. It is the kind of thing only
somebody who has played the game on hardware can confirm.

**What has still never run:** a console. Sound is still only "accepted by `sceAudioOut`" rather than
heard, and Vita3K's GPU, timing and scheduling are approximations, so the frame rate here is not a
frame rate for a Vita.

One thing to look at first on a console: the 3D depth convention in `G3SIM_AddVtx`. The drawn frames
above do not settle it, because the scenes that reached the GPU are mostly 2D with a 3D layer behind
them -- a field or a battle, where the depth ordering decides what is in front of what, is the test.

### Finding the bug that stopped it at thirty seconds

Worth writing down as a method, because the first three attempts all pointed the wrong way.

The symptom: the game ran the opening and stopped dead after about 900 frames, every time, the
emulator's own log filling with `Invalid read of uint32_t` at a PC inside `_malloc_r`. A crash inside
the allocator means somebody wrote outside a block -- and by the time malloc trips over the damage,
whatever did it is long gone. Three things were built to close that gap, and each ruled out a
suspicion rather than confirming one:

1. **A stall watchdog** (`port/vita/watchdog.c`), because a port that stops should say so. It reports
   the frame, who holds the DS execution lock, every DS thread's kernel status and how many times
   each of the port's wait sites has been entered -- a count still climbing is a spin, all of them
   still is a wait nothing will satisfy. It found nothing, which was itself the answer: the port was
   not stalled, the *process* had died and Vita3K had returned to its own window, which looks
   identical from outside.
2. **A guarded allocator** (`port/vita/heap_guard.c`, `make MALLOC_GUARD=1`), which puts a magic word
   either side of every block and checks them on free and on a sweep of all live blocks every 256
   operations. It reported no overflow at all. So nothing was writing past the end of a block.
3. **A check of libc's free-list heads**, run once a frame in every build (`VitaNativeHeapCheck`).
   `__malloc_av_`'s entries are never null in a healthy program, so a null is proof of a stray write.
   They were intact on the last frame before the crash.

What actually found it was reading the *first* fault rather than the ten thousandth. The emulator
logs registers with each one, and at the first: `PC` in `_free_r`, and the pointer being freed was
`0x817890fc` -- inside `s_HW_MAIN_MEM`, the DS's own main RAM. Something was handing the game's own
allocator's memory to libc's `free`, which then read a "chunk header" made of whatever DS data sat in
front of it and walked off into nothing.

The culprit was one line in the geometry frontend. `DRAW_CMD_G3_CMD_LIST` ended with
`free(msg->data.ptr)`, from a design where the command list was posted to a drawing thread and the
poster had to hand over a copy. This port runs the simulator synchronously on the caller's own buffer
-- which is the DS's contract for `MI_SendGXCommand`, and which lives in the game's arena. Deleting
the free is the whole fix, and with it the game runs the opening, reaches the title screen, and loops.

The lesson, and it generalises: **a crash in the allocator is a report, not a location.** The tools
that say where a block was overrun cannot see a pointer that was never a block. The register dump at
the first fault said it in one line.

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
- **An empty shader in vitaGL's cache is a hang with no message.** vitaGL writes each compiled
  shader to `ux0:data/shader_cache` and creates the file before it has anything to put in it, so a
  run killed during that first compile leaves a zero-byte `.gxp` behind. Every later run then loads
  it, gets nothing, and stops before its first frame -- with no error anywhere, because as far as
  vitaGL is concerned the shader was cached. `tests/vita/boot.sh` deletes empty ones before every
  run.
- **A crash in `_malloc_r` is not a heap overflow.** It can be a pointer that was never a heap block
  at all: the DS's allocator hands out memory inside `s_HW_MAIN_MEM`, and one call to libc's `free`
  with one of those sends the allocator walking a chunk header made of DS data. The tools that catch
  overruns cannot see it. See the section above for what did.
- **The emulator will fill the disk.** Vita3K at the log level worth running writes several
  gigabytes a minute, to its own `vita3k.log` as well as to its standard output; a five-minute boot
  produced 29 GB across the two and the next build failed on a full disk. `tests/vita/emulator.sh`
  now runs a guard alongside it that truncates either file past 64 MB. Truncating a file the writer
  still holds open leaves a hole rather than rewinding it, so the emulator keeps appending where it
  was and the space comes back.
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
| Pinned VitaSDK toolchain | Done (`scripts/toolchain.sh`), and optional: `VITASDK` uses an install you already have, with the build tree invalidated when the toolchain changes |
| vitaGL, vitaShaRK and math-neon, pinned and built into the toolchain | Done (`scripts/deps.sh`); the renderer needs `libshacccg.suprx` at runtime |
| Platform layer: arena, tick, interrupts, threads, alarms, touch, clock | Done; 27 runtime checks pass in Vita3K |
| Build machinery: one tree, placeholders for the target's flags | Done (`scripts/stage.sh`, `port/build/vita.mak`) |
| DS SDK replacement compiled for ARM | Done: 270/271, and the one failure is a module the filter drops |
| The game's own 1016 C files compiled for ARM | Done: 1016/1016 |
| DS message queues, mutexes, cache maintenance | Done (`port/vita/os_sync.c`), with the execution lock released across every blocking wait |
| Frame driver, entry point, log, save file, owner profile | Done (`port/vita/frame.c`, `app_main.c`, `memlog.c`, `backup.c`, `owner_info.c`) |
| Vertical blank | Done: the console's own, through `sceDisplayWaitVblankStart` (`port/vita/cadence.c`) |
| Renderer: DS 2D composed in software, put on the display through vitaGL | Works, drawn and seen: the opening cutscene, the title screen and Rowan's intro, in colour, both panels |
| Renderer: DS 3D rasterised on the GPU | Runs: up to 4240 polygons a frame, 123 textures cached at a 98% hit rate. The depth convention still needs a field or battle scene to confirm |
| Audio: the DS mixer's output through `sceAudioOut` | Working: 500k samples accepted, peak 21912/32767, 15 channels -- real audio from the ROM, not yet heard through a speaker |
| Overlay data layout and the link | Done (`overlays/gen-link.py`, `INSERT AFTER .data`) |
| VPK packaging | Done, and wired into `./build.sh game` |
| The game's data | Unpacked from a ROM at build time and packed into the VPK (`scripts/extract_assets.py`, `roms/`), so the console needs nothing else; a build with no ROM still works and reads one from the memory card |
| Screen layout: both DS screens side by side, 480x360 each | Done (`port/vita/include/vitapoke.h`, honoured by the renderer and by touch); exactly the DS's 4:3, nothing cropped, nothing overlapping |
| Touch input | Done: `TP_*` reads the front panel through `sceTouch`, no cursor and no stylus mode |
| Buttons | Done, and verified on the running game: `SCE_CTRL_START` reaches the DS START bit (`./build.sh boot --press`) |
| On-screen keyboard | Not needed: the game's own naming screen is used, because the Vita has a touchscreen |
| MIPS inline assembly | Done: `mov %0, sp` and `dmb ish`, guarded on `__arm__` |
| Runtime checks in the Vita3K emulator | Done (`./build.sh emu-check`) |
| Does it boot? | Yes, in Vita3K, from a real ROM: startup, the opening cutscene, the title screen, and back round the attract loop -- 9000 frames at 30 fps with no assertion or fatal |
| Does the DS 2D renderer work? | Yes: the copyright screen, the GAME FREAK logo, the Pokémon logo and the title screen, in colour, both screens |
| Stall watchdog and heap diagnostics | Done (`port/vita/watchdog.c` always on, `heap_guard.c` under `MALLOC_GUARD=1`) |
| Does the GPU path work? | Yes, with `libshacccg.suprx` in the emulator: shaders compile on first present, both panels and the 3D layer are drawn, 22-30 fps under a software GL driver |
| Does it play? | **It starts, drawn on the GPU.** START and A take it from the title screen through the main menu into a new game, and further presses advance Rowan's dialogue. No sound has been heard and nothing has run on hardware |
| SoulSilver | Not started: its sources are here, only Platinum has a build driver |
