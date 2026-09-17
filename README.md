# pspoke

![Status: beta](https://img.shields.io/badge/status-beta-yellow) ![Platform: PSP](https://img.shields.io/badge/platform-PSP-blue) [![Release](https://img.shields.io/github/v/release/IbrahimIrfan/pspoke?include_prereleases&label=release)](https://github.com/IbrahimIrfan/pspoke/releases) [![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-green)](LICENSE)

Pokémon Platinum and SoulSilver running **natively on a PSP**. The game is compiled into a real PSP program
(it is not an emulator), so it runs at close to full speed on real hardware (tested on a PSP-3001).

> **Beta.** pspoke is still in beta: expect bugs, and keep backups of your saves. Bug reports and contributions are
> very welcome, see [Bug reports and contributing](#bug-reports-and-contributing).

> **Bring your own ROM.** This repository contains **no** game code, graphics, music, text, ROMs, saves or prebuilt
> EBOOTs. It is a set of build scripts, PSP platform code and patches. You build the EBOOT yourself, on your own
> computer, from the community decompilation projects and **your own legally dumped cartridge**.
>
> pspoke is not associated with or endorsed by Nintendo, Game Freak, Creatures or The Pokémon Company. Please
> don't ask for, or post links to, ROMs or prebuilt builds in issues.

> **No warranty.** pspoke is provided as is, without warranty of any kind. Use it at your own risk: the author is not
> liable for anything that happens to your PSP, memory stick, saves or computer, and building it yourself is your
> responsibility. It is your responsibility to comply with the laws that apply to you, including only using a dump
> of a game you own. See the [LICENSE](LICENSE) (GPL-3.0, sections 15 and 16).

> **Written by AI.** The code, patches, tests and documentation in this repository were written by AI.

## Status

| Game | ROM the build accepts (SHA1) | State |
|---|---|---|
| Pokémon Platinum (US, Rev 1) | `0862ec35b24de5c7e2dcb88c9eea0873110d755c` | Playable. Around 30 fps most of the time, dropping during heavier action. |
| Pokémon SoulSilver (US) | `f8dc38ea20c17541a43b58c5e6d18c1732c7e582` | Playable. Around 30 fps most of the time, dropping during heavier action. |

Sound works. Both games include a few [quality-of-life changes](#quality-of-life-changes); each can be turned off at
build time.

## Not supported yet

- **Wi-Fi and online features:** GTS, Wi-Fi Plaza, online trades and battles, Mystery Gift over the internet.
- **DS wireless:** local trades and battles with another DS, Union Room, DS Download Play.
- **Microphone** features.
- **Other versions and regions:** only the two US ROMs listed above build (no HeartGold, Diamond/Pearl or
  non-English releases).
- **Windows:** untested for building and running the games; the toolchain setup is confirmed on WSL2 (Ubuntu).
  Building works on macOS and Linux.

A **native PS Vita port** is in progress and is now the project's target. The toolchain, the platform
layer (threads, timing, touch input) and the packaging path work, and the DS SDK and all 1016 of the
game's C files compile for ARM (`./build-vita.sh game`). It cannot produce a runnable build yet: the
renderer, the audio backend and the link step are still to be written. See
[docs/VITA.md](docs/VITA.md).

The **PSP build** (`./build.sh`) still exists but is no longer maintained or verified.

## What you need

- A PSP-2000, 3000, Go or E1000, or a PS Vita/PSTV running Adrenaline, with custom firmware that can launch
  homebrew (tested: PSP-3001 on ARK-4). **The PSP-1000 does not work** (see
  [issue #10](https://github.com/IbrahimIrfan/pspoke/issues/10)): it has 32 MB of RAM and pspoke needs about 38 MB,
  and that comes from the port's memory layout, not a setting. Making it fit is an open issue.
- Your own ROM dump of the game (the build checks the SHA1 above).
- A Mac or a Linux PC with about 3 GB of free disk space and an internet connection.

You don't need to install the PSP toolchain or any libraries yourself: the build downloads a pinned copy of the
PSP compiler ([PSPDEV](https://github.com/pspdev/pspdev), about 150 MB) into the project folder the first time.

## Building

```sh
git clone https://github.com/IbrahimIrfan/pspoke.git
cd pspoke
./build.sh platinum   --rom "/path/to/Platinum.nds"
./build.sh soulsilver --rom "/path/to/SoulSilver.nds"
```

`build.sh` checks for the few tools it needs (git, python3, make, patch, rsync, curl) and offers to install any that
are missing. Details and per-platform notes (macOS, Linux, Windows/WSL) are in [docs/INSTALL.md](docs/INSTALL.md).

`build.sh` verifies the ROM's SHA1 (see the table above), and on the first run downloads the pinned toolchain
(about 150 MB) and the decompilation sources (about 1 GB), so the first build takes 10-20 minutes; incremental
builds are much faster. The output is `dist/platinum/NativePlatinum/EBOOT.PBP` or
`dist/soulsilver/NativeSoulSilver/EBOOT.PBP`.

Options:

- `./build.sh setup` checks the requirements and fetches the toolchain without building.
- `./build.sh clean` removes the build output (downloads are kept).
- `PSPDEV=/path/to/pspdev` uses an existing toolchain instead of the downloaded one.
- `--dev` builds the instrumented version described under [Developer builds](#developer-builds).
- For a menu icon and background, put `ICON0.PNG` (144x80) and `PIC1.PNG` (480x272) in `art/platinum/` or
  `art/soulsilver/` before building (see [art/README.md](art/README.md)).

## Quality of Life changes

Both games get a few small changes. They are all on by default, and each can be turned off with a build flag
(`./build.sh platinum --rom ... --no-trade-evos`, for example). They only change how the game behaves in RAM; your
ROM and the save format are untouched, so a save moves between builds with different flags.

| Change | Flag to turn it off |
|---|---|
| Instant text | `--no-instant-text` |
| Trade evolutions without trading | `--no-trade-evos` |
| "Use another?" prompt when a Repel wears off | `--no-repel-prompt` |
| HM moves can be forgotten | `--no-forget-hms` |
| Cut, Rock Smash and Whirlpool buffed | `--no-move-buffs` |

[docs/QOL.md](docs/QOL.md) has the details: every affected Pokémon and item, and what differs between the two games.
Changing a flag only rebuilds the handful of files it touches, so switching is quick.

## Install on the PSP

Connect the memory stick (USB mode) and run:

```sh
scripts/install.sh platinum   "/Volumes/<memory stick>" "/path/to/Platinum.nds"
scripts/install.sh soulsilver "/Volumes/<memory stick>" "/path/to/SoulSilver.nds"
```

This creates `PSP/GAME/NativePlatinum/` (or `NativeSoulSilver/`) with the EBOOT, a copy of your ROM and, only if you
don't already have one there, a blank save. It never overwrites an existing save. To do it by hand:

```
PSP/GAME/NativePlatinum/EBOOT.PBP                <- dist/platinum/NativePlatinum/EBOOT.PBP
PSP/GAME/NativePlatinum/Platinum.nds             <- your ROM
PSP/GAME/NativePlatinum/Platinum.native.sav      <- python3 scripts/make_save.py Platinum.native.sav (new game)

PSP/GAME/NativeSoulSilver/EBOOT.PBP              <- dist/soulsilver/NativeSoulSilver/EBOOT.PBP
PSP/GAME/NativeSoulSilver/SoulSilver.nds         <- your ROM
PSP/GAME/NativeSoulSilver/SoulSilver.native.sav  <- python3 scripts/make_save.py SoulSilver.native.sav
```

Then launch it from the PSP's Game menu. Keep the CPU at the default speed; the game sets 333 MHz itself.

**Controls:** the PSP buttons map to the DS buttons (○ = A, ✕ = B, △ = X, □ = Y, L, START, SELECT, D-pad). R is
used to swap screens, so the DS R button is not available (the games barely use it). The two DS screens share the
PSP display, one large and one small, and the touch screen is emulated with a cursor:

- **R** (or SELECT + ✕) swaps which screen is large. Normally the top screen is the big one; in stylus mode the
  touch screen takes the big slot so you can see what you are pointing at.
- In stylus mode the **analog stick** moves the cursor (push further to move faster) and **L** taps the screen.
  The D-pad and the other buttons keep working normally, so you can mix touch and button input.
- **L + R + SELECT** quits the game back to the PSP menu.

**Saves:** `Platinum.native.sav` / `SoulSilver.native.sav` are normal 512 KB DS saves. You can bring over a save from a DS emulator or
cartridge dump by renaming it (it must be exactly 524,288 bytes). Back it up before experimenting.

## Developer builds

```sh
./build.sh platinum   --rom "/path/to/Platinum.nds"   --dev
./build.sh soulsilver --rom "/path/to/SoulSilver.nds" --dev
```

`--dev` builds `dist/platinum-dev/` or `dist/soulsilver-dev/` with an on-screen fps counter and performance lines
written to `native-memlog.txt` next to the EBOOT: frame timing and GPU sync stats for both games, plus a
game-thread profile and a 3D renderer profile on Platinum. Normal builds have no on-screen counter and only log
errors. See [docs/DEVELOPING.md](docs/DEVELOPING.md).

## Bug reports and contributing

pspoke is a beta, so reports and help are very welcome. Check the [open issues](https://github.com/IbrahimIrfan/pspoke/issues)
first; known bugs are tracked there.

**Found a bug?** Open an issue at [github.com/IbrahimIrfan/pspoke/issues](https://github.com/IbrahimIrfan/pspoke/issues) with:
- the game and what happened (what you did right before, and whether it happens again),
- your PSP model and firmware, and your computer's OS if the build failed,
- the pspoke version (`git log -1 --oneline`),
- if you can, `native-memlog.txt` from the game's folder on the memory stick (a `--dev` build logs more detail).

Please don't attach or link ROMs, saves from someone else's game, or prebuilt EBOOTs.

**Want to contribute?** Pull requests are welcome (run `tests/run.sh` first; see [tests/README.md](tests/README.md)), especially for:
- **Windows support** (building natively or confirming WSL works),
- **other games:** Diamond/Pearl, HeartGold, and other regions or languages of Platinum/SoulSilver,
- anything under [Not supported yet](#not-supported-yet),
- testing on other PSP models and firmware.

[docs/DEVELOPING.md](docs/DEVELOPING.md) explains how the build and the code are laid out, and [NOTES.md](NOTES.md)
collects the hardware, porting and testing lessons learned so far (read it before debugging on a real PSP). The same rules apply to
contributions: no ROM data, game assets or prebuilt EBOOTs in commits (changes to the decompilations go in `patches/`).

## How it works

1. `build.sh` checks your ROM and downloads the decompilation and NitroSDK-replacement sources at pinned commits
   (`third_party.lock`).
2. It applies pspoke's patches (`patches/`) and generates headers from the decompilation's data files.
3. It compiles the game's C code, the SDK libraries and pspoke's PSP platform layer (`port/`: memory/register
   emulation, threads, audio, file system, save, input, on-screen keyboard) for the PSP's MIPS CPU.
4. The DS 3D and 2D graphics commands are translated to the PSP GPU by pspoke's renderer (derived from melonDS).
5. Everything is linked into `EBOOT.PBP`, which is checked against the PSP firmware loader's limits.

At runtime the game reads its data files (graphics, maps, sound) from your ROM on the memory stick.

## Credits and licenses

pspoke would not exist without the work of other people. It is a thin layer on top of years of reverse engineering
and tooling by others, and the people behind that work deserve the credit far more than this project does. In
particular: **[pret](https://github.com/pret)** and its contributors, whose Platinum and HeartGold/SoulSilver
decompilations are what everything here is built on; **[cybervisi0n](https://github.com/cybervisi0n)**, whose PC
port of Platinum and libntr SDK replacement are the foundation pspoke builds on; **[antonsynd](https://github.com/antonsynd)**
for the HeartGold/SoulSilver decompilation fork that made SoulSilver possible; **[RoadrunnerWMC](https://github.com/RoadrunnerWMC)**
for ndspy; **[lhearachel](https://github.com/lhearachel)** for metang; and the
[melonDS](https://github.com/melonDS-emu/melonDS) and [PSPDEV](https://github.com/pspdev) contributors.
If you find pspoke useful, their projects are the ones to thank and support.

- [pret](https://github.com/pret) and contributors: the Pokémon decompilation projects.
- [cybervisi0n/pokeplatinum](https://github.com/cybervisi0n/pokeplatinum), [libntr](https://github.com/cybervisi0n/libntr)
  (MIT), libntrsystem, libntrdwc, libntrwifi, libvct: Platinum port base and NitroSDK replacement.
- [antonsynd/pokeheartgold-slop](https://github.com/antonsynd/pokeheartgold-slop): HeartGold/SoulSilver decompilation fork.
- [melonDS](https://github.com/melonDS-emu/melonDS) (GPL-3.0): the DS 2D renderer pspoke's renderer is derived from.
- [pret/pokeheartgold](https://github.com/pret/pokeheartgold): used to check SoulSilver asset ordering.
- [ndspy](https://github.com/RoadrunnerWMC/ndspy) (GPL-3.0, bundled): reads the overlay table from your SoulSilver ROM.
- [metang](https://github.com/lhearachel/metang), [PSPDEV](https://github.com/pspdev).
- Vita port only: [vitaGL](https://github.com/Rinnegatamante/vitaGL) (LGPL-3.0) for OpenGL over GXM,
  [math-neon](https://github.com/Rinnegatamante/math-neon) (MIT), [vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK)
  (LGPL-3.0, header only) and [VitaSDK](https://vitasdk.org). All downloaded at build time, not included here.

pspoke is licensed under the **GNU General Public License v3.0** (`LICENSE`) because its renderer is derived from
melonDS. Third-party sources keep their own licenses and are downloaded at build time, not included here.
The games are the property of their respective owners.
