# Developing pspoke

## Layout

| Path | What it is |
|---|---|
| `build.sh` | Entry point: ROM check, then `scripts/<game>.sh`. |
| `build-vita.sh` | Entry point for the PS Vita port (`setup`, `check`, `emu-check`, `clean`); see [docs/VITA.md](VITA.md). |
| `scripts/` | Build steps (`fetch.sh`, `stage.sh`, `platinum.sh`, ...), `prereqs.sh` (checks git/python3/make/patch/rsync/curl/tar and offers to install what is missing; `PSPPOKE_ASSUME_YES=1` skips the prompt), `install.sh`, `make_save.py`, `check_native_pbp.py` (PSP loader limits). |
| `port/` | pspoke's own code, laid out as the build tree expects (`port/<component>/...`). |
| `port/vita/` | The Vita platform layer: the DS interfaces (`OS_*`, `TP_*`, `RTC_*`) implemented on psp2. `os_core.c` (arena, tick, interrupts), `os_thread.c`, `os_alarm.c`, `input.c` (pad and the real touchscreen), `sdl_sync.c` + `sdl2-shim/` (the few SDL types libntr's headers want), `shark_stub.c` (keeps vitaGL from needing a runtime shader compiler). |
| `patches/` | Patches applied to the downloaded decompilations and to generated per-overlay source copies. |
| `docs/QOL.md` | Every quality-of-life change, per Pokémon/item, and its build flag. |
| `docs/INSTALL.md` | Prerequisites and per-platform install notes (macOS, Linux, Windows/WSL). |
| `tests/` | Regression suite (`run.sh`), synthetic save fixtures, `tests/README.md`. |
| `tests/vita/` | Vita checks that need no ROM: `run.sh` compiles and links the DS interfaces the platform layer implements, `vita3k.sh` runs them in the Vita3K emulator. |
| `third_party/melonDS/` | The four melonDS headers the renderer includes (GPL-3.0). |
| `.cache/upstream/` | Downloaded pinned sources (created by the build). |
| `.cache/vitasdk/` | Downloaded pinned VitaSDK plus vitaGL (created by `./build-vita.sh setup`). |
| `.work/tree/` | The staged build tree shared by both games (created by the build; safe to delete). |
| `dist/` | Built EBOOTs. |

Main components in `port/` (Platinum):

- `native-audio-app/`: the PSP program. `main.c` (boot, ROM and save paths), `frame.c` (frame loop, pacing, DEV logging),
  `input.c`, `osk.c`/`naming_osk.c` (PSP on-screen keyboard for name entry), `platform.c`, `services/` (file system from
  the ROM, DMA, locks, power). `overlays/` turns the game's DS overlays into native modules (`generate.py`, `build.py`,
  `internal.py`, `gen-link.py`). `sdk-g3stack/` holds patched SDK sources (immediate 3D commands without malloc).
- `native-stack-render/`: the DS 3D-to-PSP-GPU renderer (display-list cache, fixed-point fast paths, pipelined present,
  texture change detection from VRAM writes: `native-audio-app/vram_dirty.c` + `opttex_redirect.py`).
- `native-render-opt/`: melonDS-derived 2D engine (`GPU2D_Soft.cpp`, `native_gpu.cpp`).
- `native-probe/`: header generators, register/memory backing, network/internal library compile scripts, particle fix.
- `native-audio-sound/`, `native-audio-probe/`: DS sound engine; `sas_out.c` plays its channels through the PSP sceSasCore
  voice mixer (no CPU mixing).
- Small services: `native-threads`, `native-alarms`, `native-offline`, `native-sdl-thread`, `native-cadence`,
  `native-memory-probe`, `native-backup-probe` (save file), `native-sdk-probe` (SDK compile).

SoulSilver components in `port/` (it reuses the SDK, services and renderer core above):

- `soulsilver-native-core/`: `prepare.py`/`finish_headers.py`/`final_compat.py` make a compilable copy of the
  pokeheartgold-slop sources (then `patches/soulsilver/*.patch` apply pspoke's game changes); `crossprobe.py` +
  `archive.py` compile all 529 game files. `nitromain-perf/` is the PSP program (`main.c`, `frame.c`, `input.c`,
  `osk.c`, native ports of hot functions, `linkfile.prx` with the `.native.backing` split for the 32 MiB loader limit).
- `soulsilver-native-overlays/build_registry.py`: splits game/data objects per DS overlay using your ROM's overlay table.
- `soulsilver-native-data/`: converts the decompilation's assembly data to PSP objects (`python/` bundles ndspy).
- `soulsilver-native-codegen/`, `soulsilver-native-play/maploader/`: translate not-yet-decompiled ARM assembly from the
  decompilation into C (`library.py`, `port.py`; seeds and ABI notes in the JSON files).
- Smaller native ports: `soulsilver-native-{player-movement,menu-sprites,window,sound-helpers,islands,particles,billboards}`,
  `soulsilver-native-assets/fade-port/wipe-candidate` (screen wipes) and `render-fastcompare` (SoulSilver renderer variant).
- `native-sound-audio/`: SoulSilver's sound backend (same sceSasCore output, `sas_out.c`).

Folder names are historical (each started as an isolated proof); build scripts rely on this relative layout.

## Dev vs normal builds

`./build.sh <game> --rom ... --dev` passes `DEV=1` to the renderer and app Makefiles:

- `PSP_NATIVE_DEV`: on-screen counter (`NATIVE xx.x fps game/audio/render ms`), per-30-frame timing collection,
  `[FPS]`/`[PERF]`/`[GEASYNC]` lines in `native-memlog.txt` every 600 frames.
- `PSP_NATIVE_GAME_PROF`: `[GPROF]` game-thread buckets and allocator counters (adds link-time wraps). Platinum
  only: the SoulSilver Makefile has no `GAME_PROF`.
- `PSP_NATIVE_G3_HWPROF` (Platinum renderer): sampled `[G3HW]` 3D profile.

Normal builds define none of these; error and startup lines are still logged. Use `#ifdef PSP_NATIVE_DEV` for any new
debug output. printf is invisible on real hardware, so log through `PSPNativeMemLog`.

## Iterating

After a full build, edit files directly in `.work/tree/test_out/...` and rerun `./build.sh`: finished phases are
skipped via `.work/tree/.stamp-*` files (delete a stamp to redo that phase), and the renderer and app are always
relinked. Copy changes back into `port/` (or turn decompilation changes into a patch in `patches/`) before committing.
If you edit `port/` directly instead, run `./build.sh clean` before rebuilding (downloads in `.cache` are kept): staging
copies `port/` with its original timestamps, so an already-built object can look newer than your change and be reused.

## Tests

`tests/run.sh` builds each game, runs the loader audit and, with `PPSSPP_HEADLESS` set, replays recorded scenarios
(menus, battles, evolutions, the QoL features) in headless PPSSPP and checks the game's log. Run it before a pull
request; `tests/README.md` explains the scenarios, fixtures and how to add one.

`tests/vita/run.sh` (or `./build-vita.sh check`) runs the Vita port's checks, which need no ROM: that
every DS interface `port/vita` implements still compiles and links against libntr and psp2, and that
the GPU still offers the paletted textures, stencil and render-to-texture the renderer design depends
on. A VitaSDK or libntr bump is what this catches.

`tests/vita/vita3k.sh` (or `./build-vita.sh emu-check`) runs the platform layer in the Vita3K emulator
and checks what it does rather than that it builds: thread serialisation under the DS execution lock
(with an unlocked control, so the test can fail), sleep/wake, alarms, and the psp2 semantics the design
rests on. It downloads the emulator and takes a couple of minutes.

Other checks:
- `python3 scripts/check_native_pbp.py dist/.../EBOOT.PBP`: the retail PSP loader rejects EBOOTs with a section ending
  past 32 MiB (error 80020148). Every build runs this; PPSSPP does not enforce it.
- Diagnostic make variables (test links only, never in `./build.sh` output): Platinum `WARP_TO=<map>,<x>,<z>`
  (`diag_warp.c`) and `QOL_TEST`; SoulSilver `WARP_TO`, `NO_WILD`, `GIVE_SPECIES`, `REPEL_STEPS`, ... (see its
  Makefile). `tests/run.sh` uses them to set scenes up.
- One-off emulator runs: `run_probe.py` in `port/native-audio-app/` and `port/soulsilver-native-core/nitromain-perf/`
  stage a throwaway memory stick and run the EBOOT (`--ppsspp`, `--rom`, `--save`/`--fixture`, `--seconds`;
  SoulSilver also takes `--input-script` and `--frames`). Emulator timings say nothing about PSP speed.

## Rules for contributions

- Never commit ROMs, saves, extracted game data, compiled objects or EBOOTs (`.gitignore` covers them).
- Decompilation changes go in `patches/`, not as copied source files.
- Keep melonDS/libntr license headers in derived files.
