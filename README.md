# vitapoke

![Status: in progress](https://img.shields.io/badge/status-in%20progress-orange) ![Platform: PS Vita](https://img.shields.io/badge/platform-PS%20Vita-blue) [![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-green)](LICENSE)

Pokémon Platinum and SoulSilver compiled to run **natively on a PS Vita**. Not an emulator: the game's own
code is built for the Vita's ARM CPU and linked into a Vita application.

> **It does not run yet.** This is a port in progress, not something you can play. What works today is
> the build: the toolchain, the platform layer, and the game's own code compiling for ARM. The renderer,
> the audio backend and the link step are still to be written, so there is no VPK at the end of it. See
> [docs/VITA.md](docs/VITA.md) for exactly where the line is.
>
> vitapoke started as a port of [pspoke](https://github.com/IbrahimIrfan/pspoke), which does the same
> thing for the PSP and is playable today. If you want to play rather than build, use that.

> **Bring your own ROM.** This repository contains **no** game code, graphics, music, text, ROMs, saves or
> prebuilt executables. It is a set of build scripts, Vita platform code and patches. You build it
> yourself, on your own computer, from the community decompilation projects and **your own legally dumped
> cartridge**.
>
> vitapoke is not associated with or endorsed by Nintendo, Game Freak, Creatures or The Pokémon Company.
> Please don't ask for, or post links to, ROMs or prebuilt builds in issues.

> **No warranty.** vitapoke is provided as is, without warranty of any kind. Use it at your own risk: the
> author is not liable for anything that happens to your Vita, memory card, saves or computer, and
> building it yourself is your responsibility. It is your responsibility to comply with the laws that
> apply to you, including only using a dump of a game you own. See the [LICENSE](LICENSE) (GPL-3.0,
> sections 15 and 16).

> **Written by AI.** The code, patches, tests and documentation in this repository were written by AI.

## Status

| Piece | State |
|---|---|
| Pinned VitaSDK toolchain, fetched and verified | Works |
| Platform layer: threads, timing, alarms, arena, pad, **real touchscreen** | Works; 27 runtime checks pass in the Vita3K emulator |
| DS SDK replacement compiled for ARM | Works (270/271; the one failure is a module the build drops) |
| The game's own 1016 C files compiled for ARM | Works (1016/1016) |
| Renderer (DS 2D/3D → GXM) | Not written |
| Audio (the DS sound engine's output) | Not written |
| Link step: overlay layout, VPK packaging | Not written |
| SoulSilver build driver | Not written (its sources are here; only Platinum has a driver) |

Only Platinum and SoulSilver, US releases, as in pspoke. Wi-Fi, DS wireless and microphone features are
not ported and are not planned.

## What you need

- A Mac or a Linux PC, about 3 GB of free disk space and an internet connection.
- Your own ROM dump — but **not yet**: everything that currently builds works without one.

You don't need to install VitaSDK or any libraries yourself. The build downloads a pinned
[VitaSDK](https://vitasdk.org) snapshot (about 100 MB) into the project folder the first time, and builds
its GPU dependencies there too.

A Vita is not needed to work on this, and is not enough to test it: see
[Testing](#testing).

## Building

```sh
git clone https://github.com/martepato/vitapoke.git
cd vitapoke
./build.sh setup     # pinned VitaSDK + vitaGL, about 100 MB, one time
./build.sh game      # the DS SDK and the game's own code, compiled for ARM
```

`./build.sh game` fetches the pinned decompilation sources (about 300 MB), generates the decompilation's
headers, compiles the DS SDK replacement and then all 1016 of the game's C files. About six minutes from
a clean tree on four cores. It stops there — there is nothing to link yet.

Commands:

- `./build.sh setup` — check host tools, download the pinned toolchain and build its dependencies.
- `./build.sh game [--rom FILE]` — build as far as the port reaches. The ROM is only needed for the
  per-overlay bounds table, which the link step will want; everything before that works without it.
- `./build.sh check` — the checks that need no ROM and no emulator (a few seconds).
- `./build.sh emu-check` — run the platform layer in the Vita3K emulator (downloads it; a few minutes).
- `./build.sh clean` — remove the build output; downloads in `.cache` are kept.
- `VITASDK=/path/to/vitasdk` uses an existing install instead of the downloaded one.

Finished phases are skipped on a rerun via stamp files in `.work/vita/`; delete one to redo that phase.

## Testing

`./build.sh check` compiles and links the platform layer and the GPU features the renderer needs — a
contract check, in seconds.

`./build.sh emu-check` is the interesting one. It builds a test into a VPK, boots it in the
[Vita3K](https://vita3k.org) emulator and reads back the report, which is the only way short of hardware
to check what the platform layer *does* rather than that it builds. It is how the DS execution lock was
shown to work: two DS threads that interleave 248 times without it, and 0 times with it.

Vita3K is not a console. Timing, scheduling and touch are approximations, so hardware is still the last
word — but it catches a great deal first.

## Quality of Life changes

Carried over from pspoke and still wired into the build, though nothing runs them yet. All on by default;
each can be turned off by setting its switch (`VITAPOKE_QOL_TRADE_EVOS=0`, for example) when building.
They only change how the game behaves in RAM; your ROM and the save format are untouched.

| Change | Switch |
|---|---|
| Instant text | `VITAPOKE_QOL_INSTANT_TEXT` |
| Trade evolutions without trading | `VITAPOKE_QOL_TRADE_EVOS` |
| "Use another?" prompt when a Repel wears off | `VITAPOKE_QOL_REPEL_PROMPT` |
| HM moves can be forgotten | `VITAPOKE_QOL_FORGET_HMS` |
| Cut, Rock Smash and Whirlpool buffed | `VITAPOKE_QOL_MOVE_BUFFS` |

[docs/QOL.md](docs/QOL.md) has the details: every affected Pokémon and item, and what differs between the
two games.

## Controls

Planned, and mostly not implemented yet — the renderer decides the screen layout and nothing draws.
What the platform layer already does:

- The Vita's buttons map to the DS buttons (○ = A, ✕ = B, △ = X, □ = Y, L, R, START, SELECT, D-pad).
  Unlike the PSP build, **R keeps its DS meaning**: there is no screen-swap mode to bind it to.
- The DS touch screen is **touched**. `TP_*` reads the front panel through `sceTouch`, so there is no
  cursor and no stylus mode.

Saves will be normal 512 KB DS saves, movable to and from a DS emulator or cartridge dump.
`python3 scripts/make_save.py <name>.sav` writes a blank one.

## Contributing

The port is early, and the open pieces are large and fairly independent:

- **the renderer** — the DS 2D compositor and 3D output on GXM (the design, and what is already settled
  about it, is in [docs/VITA.md](docs/VITA.md)),
- **audio** — the DS sound engine's 16 channels out through `sceAudioOut` or `sceNgs`,
- **the link step** — the overlay layout's linker script, and VPK packaging,
- **the on-screen keyboard** — name entry through `sceIme`,
- **a SoulSilver build driver**, alongside Platinum's.

Run `./build.sh check` before a pull request, and `./build.sh emu-check` if you touched the platform
layer. [docs/DEVELOPING.md](docs/DEVELOPING.md) explains how the build and the code are laid out, and
[docs/VITA.md](docs/VITA.md) is the working record of what has been decided and why — read it first.
[NOTES.md](NOTES.md) collects the hardware and porting lessons from the PSP original; much of it still
applies to the DS side of the problem.

No ROM data, game assets or prebuilt executables in commits. Changes to the decompilations go in
`patches/`.

## How it works

1. `build.sh` downloads the decompilation and NitroSDK-replacement sources at pinned commits
   (`third_party.lock`).
2. It applies vitapoke's patches (`patches/`) and generates headers from the decompilation's data files.
3. It compiles the game's C code, the SDK libraries and vitapoke's Vita platform layer (`port/vita/`:
   the DS OS interfaces on psp2 — threads, alarms, timing, the arena, pad and touch) for the Vita's ARM
   CPU.
4. The DS 3D and 2D graphics commands will be translated to the Vita's GPU by vitapoke's renderer
   (derived from melonDS). **This part does not exist yet.**
5. Everything will be linked into a VPK.

At runtime the game reads its data files (graphics, maps, sound) from your ROM on the memory card.

## Credits and licenses

vitapoke would not exist without the work of other people. It is a thin layer on top of years of reverse
engineering and tooling by others, and the people behind that work deserve the credit far more than this
project does. In particular: **[IbrahimIrfan](https://github.com/IbrahimIrfan/pspoke)**, whose pspoke is
what this is a port of; **[pret](https://github.com/pret)** and its contributors, whose Platinum and
HeartGold/SoulSilver decompilations are what everything here is built on;
**[cybervisi0n](https://github.com/cybervisi0n)**, whose PC port of Platinum and libntr SDK replacement
are the foundation; **[antonsynd](https://github.com/antonsynd)** for the HeartGold/SoulSilver
decompilation fork that made SoulSilver possible; **[RoadrunnerWMC](https://github.com/RoadrunnerWMC)**
for ndspy; **[lhearachel](https://github.com/lhearachel)** for metang; and the
[melonDS](https://github.com/melonDS-emu/melonDS), [VitaSDK](https://vitasdk.org) and
[Vita3K](https://vita3k.org) contributors. If you find vitapoke useful, their projects are the ones to
thank and support.

- [IbrahimIrfan/pspoke](https://github.com/IbrahimIrfan/pspoke) (GPL-3.0): the PSP port this is derived from.
- [pret](https://github.com/pret) and contributors: the Pokémon decompilation projects.
- [cybervisi0n/pokeplatinum](https://github.com/cybervisi0n/pokeplatinum), [libntr](https://github.com/cybervisi0n/libntr)
  (MIT), libntrsystem, libntrdwc, libntrwifi, libvct: Platinum port base and NitroSDK replacement.
- [antonsynd/pokeheartgold-slop](https://github.com/antonsynd/pokeheartgold-slop): HeartGold/SoulSilver decompilation fork.
- [melonDS](https://github.com/melonDS-emu/melonDS) (GPL-3.0): the DS 2D renderer vitapoke's renderer is derived from.
- [pret/pokeheartgold](https://github.com/pret/pokeheartgold): used to check SoulSilver asset ordering.
- [ndspy](https://github.com/RoadrunnerWMC/ndspy) (GPL-3.0, bundled): reads the overlay table from your SoulSilver ROM.
- [metang](https://github.com/lhearachel/metang).
- [VitaSDK](https://vitasdk.org), [vitaGL](https://github.com/Rinnegatamante/vitaGL) (LGPL-3.0),
  [math-neon](https://github.com/Rinnegatamante/math-neon) (MIT) and
  [vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK) (LGPL-3.0, header only). All downloaded at
  build time, not included here.

vitapoke is licensed under the **GNU General Public License v3.0** (`LICENSE`) because it derives from
pspoke and its renderer derives from melonDS. Third-party sources keep their own licenses and are
downloaded at build time, not included here. The games are the property of their respective owners.
