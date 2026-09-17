# Developing pspoke

## Layout

| Path | What it is |
|---|---|
| `build.sh` | The entry point (`setup`, `check`, `game`, `emu-check`, `clean`); see [docs/VITA.md](VITA.md). |
| `scripts/game.sh` | The Vita build: stage for ARM, generate headers, compile the SDK and the game's own code. |
| `port/build/` | `vita.mak`, the make fragment every component Makefile includes. |
| `scripts/` | `toolchain.sh` (pinned VitaSDK), `deps.sh` (vitaGL, math-neon, the SDL declarations libntr wants), `fetch.sh` (pinned upstream sources), `stage.sh` (lays out the build tree), `game.sh` (the build), `prereqs.sh` (checks host tools and offers to install what is missing; `VITAPOKE_ASSUME_YES=1` skips the prompt), `make_save.py`, `png_embed.py`, `sdk_archive.py`. |
| `port/` | pspoke's own code, laid out as the build tree expects (`port/<component>/...`). |
| `port/vita/` | The Vita platform layer: the DS interfaces (`OS_*`, `TP_*`, `RTC_*`, `CARD_*`) implemented on psp2, plus the application itself. `os_core.c` (arena, tick, interrupts, the execution lock), `os_thread.c`, `os_alarm.c`, `os_sync.c` (message queues, mutexes, cache maintenance), `input.c` (pad and the real touchscreen), `cadence.c` (the console's vertical blank), `backup.c` (the 512 KB save), `owner_info.c` (the DS's firmware profile), `audio_out.c` (`sceAudioOut`), `frame.c` (the frame), `app_main.c` (the entry point), `memlog.c` (the log, the failure path, a working `abort`), `scene_log.c`, `sdl_sync.c` + `sdl2-shim/`, `shark_stub.c`. |
| `port/native-vita-render/` | The renderer: `render-vita.cpp` (the frame and the DS 2D compositor), `g3_backend.cpp` (the DS's 3D, and its texture cache), `gpu.h`/`gpu.cpp` (everything asked of the GPU, and the only file that includes a GL header). |
| `patches/` | Patches applied to the downloaded decompilations and to generated per-overlay source copies. |
| `docs/QOL.md` | Every quality-of-life change, per Pokémon/item, and its build flag. |
| `docs/INSTALL.md` | Prerequisites and per-platform install notes (macOS, Linux, Windows/WSL). |
| `tests/` | Regression suite (`run.sh`), synthetic save fixtures, `tests/README.md`. |
| `tests/vita/` | Vita checks that need no ROM: `run.sh` compiles and links the DS interfaces the platform layer implements, `vita3k.sh` runs the platform layer's runtime checks in the Vita3K emulator, `boot.sh` boots the built game there and prints its log, `emulator.sh` is the emulator bring-up both share, `make_probe_rom.py` writes a ROM-shaped file with no game data in it. |
| `third_party/melonDS/` | The four melonDS headers the renderer includes (GPL-3.0). |
| `.cache/upstream/` | Downloaded pinned sources (created by the build). |
| `.cache/vitasdk/` | Downloaded pinned VitaSDK plus vitaGL (created by `./build.sh setup`). |
| `.work/vita/` | The staged build tree (created by the build; safe to delete). Stamp files there mark finished phases; delete one to redo that phase. |
| `dist/` | Where `./build.sh game` puts `vitapoke-platinum.vpk`. |

Main components in `port/` (Platinum):

- `native-audio-app/`: the application's build and the parts of it that are not console specific.
  `config.c`, `services/` (the file system reading the ROM, DMA, the cartridge's absence, power), and
  `overlays/`, which turns the game's DS overlay modules into always-resident ones
  (`generate.py`, `build.py`, `internal.py`, `gen-link.py`, `overlay.c`). Its `Makefile` is the link:
  it lists what comes from `port/vita`, what comes from the other components, and what the linker has
  to be told (the overlay sections, the module constructors, the wraps).
- `native-vita-render/`: the renderer. See the table above and [VITA.md](VITA.md) for why it composes
  the DS's 2D in software and what it asks the GPU for.
- `native-render-opt/`: melonDS-derived DS 2D engine (`GPU2D_Soft.cpp`, `native_gpu.cpp`) and libntr's
  geometry simulator (`g3_handler.cpp`, `frontend.cpp`). Compiled unchanged into the renderer: none of
  it knows which console it is on.
- `native-probe/`: header generators, register and memory backing, the network and internal library
  compile scripts.
- `native-audio-sound/`, `native-audio-probe/`: the DS sound engine, including its own mixer
  (`sim_audio.cpp`), whose output `port/vita/audio_out.c` plays.
- Small services: `native-offline` (the unavailable network boundary), `native-memory-probe`,
  `native-backup-probe`, `native-romfs`, `native-core-proof`, `native-sdk-probe` (the SDK compile).

SoulSilver components in `port/` (it reuses the SDK, services and renderer core above). **SoulSilver has
no Vita build driver yet**: `scripts/game.sh` builds Platinum, and the equivalent for SoulSilver is still
to be written.

- `soulsilver-native-core/`: `prepare.py`/`finish_headers.py`/`final_compat.py` make a compilable copy of the
  pokeheartgold-slop sources (then `patches/soulsilver/*.patch` apply the game changes); `crossprobe.py` +
  `archive.py` compile all 529 game files. `nitromain-perf/` is the application (`main.c`, `frame.c`,
  `input.c`, `osk.c`, native ports of hot functions, and `linkfile.prx` with the `.native.backing` split
  for the PSP loader's 32 MiB limit -- a constraint that goes away with that console).
- `soulsilver-native-overlays/build_registry.py`: splits game/data objects per DS overlay using your ROM's overlay table.
- `soulsilver-native-data/`: converts the decompilation's assembly data to objects (`python/` bundles ndspy).
- `soulsilver-native-codegen/`, `soulsilver-native-play/maploader/`: translate not-yet-decompiled ARM assembly from the
  decompilation into C (`library.py`, `port.py`; seeds and ABI notes in the JSON files).
- Smaller native ports: `soulsilver-native-{player-movement,menu-sprites,window,sound-helpers,islands,particles,billboards}`,
  `soulsilver-native-assets/fade-port/wipe-candidate` (screen wipes) and `render-fastcompare` (SoulSilver renderer variant).
- `native-sound-audio/`: SoulSilver's sound backend (same sceSasCore output, `sas_out.c`).

Folder names are historical (each started as an isolated proof); build scripts rely on this relative
layout. So are the `VITAPOKE_*` macros and `VitaNative*` function names throughout `port/` -- they are
just names now, and renaming them is a mechanical pass nobody has spent the churn on.

## Staging and the toolchain

`scripts/stage.sh` copies `port/` into the build tree and fills in the placeholders that carry
build-time paths and target flags into the staged sources: `@WORK@`, `@TOOLBIN@` (the toolchain's tool
prefix), `@TARGETCC@` (the flags the whole build needs -- read the note there, `-fno-short-enums` is
load-bearing), `@SDKBUILD@`, `@BUILDMAK@` (`port/build/vita.mak`), `@SDKINC@` and `@OVERLAYLD@`.
`scripts/common.sh` derives `TOOLBIN` for the steps that run outside the staged tree. Nothing in
`port/` names the compiler.

## Dev vs normal builds

`DEV=1` passed to the renderer and app Makefiles turns on the instrumented build. The names below are
historical; they are the port's own macros, not PSP ones:

- `VITAPOKE_DEV`: on-screen counter (`NATIVE xx.x fps game/audio/render ms`), per-30-frame timing collection,
  `[FPS]`/`[PERF]`/`[GEASYNC]` lines in `native-memlog.txt` every 600 frames.
- `VITAPOKE_GAME_PROF`: `[GPROF]` game-thread buckets and allocator counters (adds link-time wraps). Platinum
  only: the SoulSilver Makefile has no `GAME_PROF`.
- `VITAPOKE_G3_HWPROF`: sampled 3D profile. Belonged to the PSP renderer; the Vita renderer reports
  its own numbers on the `[PERF]` line every 600 frames.

There is no `--dev` switch on `./build.sh`; pass `DEV=1` to the component `make` directly.

Normal builds define none of these; error and startup lines are still logged. Use `#ifdef VITAPOKE_DEV` for any new
debug output. printf is invisible on real hardware, so log through `VitaNativeMemLog`.

## Iterating

After a full build, edit files directly in `.work/vita/test_out/...` and rerun `./build.sh game`: finished
phases are skipped via `.work/vita/.stamp-*` files (delete a stamp to redo that phase). Copy changes back
into `port/` (or turn decompilation changes into a patch in `patches/`) before committing. If you edit
`port/` directly instead, run `./build.sh clean` first (downloads in `.cache` are kept): staging copies
`port/` with its original timestamps, so an already-built object can look newer than your change and be
reused.

## Tests

`tests/vita/run.sh` (or `./build.sh check`) runs the Vita port's checks, which need no ROM: that
every DS interface `port/vita` implements still compiles and links against libntr and psp2, and that
the GPU still offers the paletted textures, stencil and render-to-texture the renderer design depends
on. A VitaSDK or libntr bump is what this catches.

`tests/vita/vita3k.sh` (or `./build.sh emu-check`) runs the platform layer in the Vita3K emulator
and checks what it does rather than that it builds: thread serialisation under the DS execution lock
(with an unlocked control, so the test can fail), sleep/wake, alarms, and the psp2 semantics the design
rests on. It downloads the emulator and takes a couple of minutes.

Diagnostic make variables still exist in the component Makefiles for test links -- Platinum
`WARP_TO=<map>,<x>,<z>` (`diag_warp.c`) and `QOL_TEST`; SoulSilver `WARP_TO`, `NO_WILD`,
`GIVE_SPECIES`, `REPEL_STEPS` -- but nothing drives them yet: the scenario suite that used them replayed
recorded input in a PSP emulator and went with that console. Rebuilding an equivalent on Vita3K is open
work, and `tests/fixtures/` keeps the synthetic saves those scenarios started from.

## Rules for contributions

- Never commit ROMs, saves, extracted game data, compiled objects or EBOOTs (`.gitignore` covers them).
- Decompilation changes go in `patches/`, not as copied source files.
- Keep melonDS/libntr license headers in derived files.
