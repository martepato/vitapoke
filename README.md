# vitapoke

![Status: in progress](https://img.shields.io/badge/status-in%20progress-orange) ![Platform: PS Vita](https://img.shields.io/badge/platform-PS%20Vita-blue) [![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-green)](LICENSE)

Pokémon Platinum and SoulSilver compiled to run **natively on a PS Vita**. Not an emulator: the game's own
code is built for the Vita's ARM CPU and linked into a Vita application.

> **It runs, and it is not playable yet.** This is a port in progress. The whole build works and
> produces a VPK, and in the Vita3K emulator that VPK runs a real ROM: the game's startup, its
> overlays, its opening -- the copyright screen, the GAME FREAK logo, the Pokémon logo, in colour, on
> both screens -- and then the **title screen**. Press START and A and it goes through the main menu
> and **starts a new game**, into Professor Rowan's introduction, where further presses advance his
> dialogue. Thousands of frames at 22-30 frames a second with no assertion and no crash, composed by this port's
> own renderer -- **drawn on the Vita's GPU**, both DS screens side by side, with the DS's 3D
> rasterised there too and the DS sound engine producing real audio. Captured frames show Lucas and
> Dawn running through the opening, the title screen's starter banner, and Professor Rowan's dialogue
> box -- correct sprites, colours and fonts on both panels. They are not in this repository: they are
> the game's own artwork, and nothing of the game belongs here.
> What has *not* happened: nobody has heard the sound, and nothing has run on a real console --
> Vita3K's GPU, timing and scheduling are all approximations. See [docs/VITA.md](docs/VITA.md) for
> exactly what is known and what is not.
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
| DS message queues, mutexes, the frame driver, the save file, the log | Works (compiles and links) |
| Renderer: DS 2D composed in software, both screens on the display through vitaGL | Works, and has been looked at: the opening, the title screen and Rowan's intro, in colour |
| Renderer: DS 3D rasterised on the GPU | Works: up to 4240 polygons a frame, 123 textures cached, 98% hit rate |
| Audio: the DS mixer through `sceAudioOut` | Works: 15 channels of real audio accepted by the output port, not yet heard through a speaker |
| Link step: overlay layout, VPK packaging | Works |
| Boots, plays the opening, reaches the title screen and starts a new game, from a real ROM | Works, in the Vita3K emulator: 14400 frames at 30 fps |
| DS 2D renderer, against the real game | Works: text, sprites, palettes, both screens |
| Stall watchdog, and a guarded allocator for finding heap bugs | Works (`port/vita/watchdog.c`, `make MALLOC_GUARD=1`) |
| GPU path (presenting, and the DS's 3D) | Works in Vita3K with `libshacccg.suprx` installed; 22-30 fps under a software GL driver |
| Buttons and touch | Buttons drive the real game through its menus; touch reads the real panel |
| Playable | **Not yet**: a new game starts and draws correctly, but no sound has been heard and nothing has run on hardware |
| SoulSilver build driver | Not written (its sources are here; only Platinum has a driver) |

Only Platinum and SoulSilver, US releases, as in pspoke. Wi-Fi, DS wireless and microphone features are
not ported and are not planned.

## What you need

- A Mac or a Linux PC, about 3 GB of free disk space and an internet connection.
- Your own ROM dump. Put it at `roms/Platinum.nds` in the project folder and the build unpacks what
  the game needs out of it and packs that into the VPK, so the console needs nothing but the
  application installed. (You can also build without one and keep the ROM on the memory card
  instead — see [Putting it on the Vita](#putting-it-on-the-vita).)

You don't need to install VitaSDK or any libraries yourself. The build downloads a pinned
[VitaSDK](https://vitasdk.org) snapshot (about 100 MB) into the project folder the first time, and builds
its GPU dependencies there too. Nothing is installed outside the project folder, and nothing on your
system is changed. If you already have VitaSDK and would rather use it, see
[Using a VitaSDK you already have](#using-a-vitasdk-you-already-have).

On the console you also need **`libshacccg.suprx`**, at `ur0:data/libshacccg.suprx` or
`ur0:data/external/libshacccg.suprx` — the port looks in both. It is Sony's shader compiler, taken
from a firmware update, and most custom-firmware setups already have it: the renderer's shaders are
compiled on the console, because there is no way to build them beforehand without that compiler. If
it is missing, vitapoke says so in its log and stops rather than showing a black screen.

A Vita is not needed to work on this, and is not enough to test it: see
[Testing](#testing).

## Building

```sh
git clone https://github.com/martepato/vitapoke.git
cd vitapoke
./build.sh setup              # pinned VitaSDK + vitaGL, about 100 MB, one time
cp ~/Platinum.nds roms/       # your own dump; the build unpacks it into the VPK
./build.sh game               # everything, ending in dist/vitapoke-platinum.vpk
```

The ROM in `roms/` is used automatically; `--rom /some/path.nds` points somewhere else. The build
unpacks the 340 files of the ROM's own file system and packs them into the VPK beside the
executable, which is where the game then opens them — the same way it opened them from a cartridge,
by name. None of the ROM's code is carried: that is already compiled into the executable. A VPK
built this way is about 53 MB and contains the game, so it is yours to keep and not to share.

**Building without a ROM works too**, and is what the checks and any CI use: the VPK is then 6.5 MB
with no game data in it, and reads the ROM from `ux0:data/vitapoke/Platinum.nds` on the memory card
at run time instead. The port picks whichever it finds and says which in its log.

`./build.sh game` fetches the pinned decompilation sources (about 300 MB), generates the
decompilation's headers, compiles the DS SDK replacement, all 1016 of the game's C files, the Vita
platform layer and the renderer, and links the lot into a VPK. About eight minutes from a clean tree
on four cores.

Commands:

- `./build.sh setup` — check host tools, download the pinned toolchain and build its dependencies.
- `./build.sh game` — the whole build, ending in `dist/vitapoke-platinum.vpk`. Unpacks a ROM from
  `roms/Platinum.nds` into it if there is one; `--rom FILE` uses another; without either, the VPK
  carries no game data and reads a ROM from the memory card.
- `./build.sh check` — the checks that need no ROM and no emulator (a few seconds).
- `./build.sh emu-check` — run the platform layer in the Vita3K emulator (downloads it; a few minutes).
- `./build.sh clean` — remove the build output; downloads in `.cache` are kept.
- `VITASDK=/path/to/vitasdk` uses an existing install instead of the downloaded one — see below.

Finished phases are skipped on a rerun via stamp files in `.work/vita/`; delete one to redo that phase.
The tree also records which toolchain built it, so switching between your own VitaSDK and the pinned
one rebuilds from scratch rather than linking objects from two different compilers.

## Putting it on the Vita

Your console needs custom firmware (HENkaku/h-encore or Enso) and VitaShell, as any homebrew does.

**1. Install the VPK.** Copy `dist/vitapoke-platinum.vpk` to the Vita — over USB or FTP with
VitaShell, or on the card in a card reader — then select it in VitaShell and confirm the install. It
installs as title `VPOK00001` (so, `ux0:app/VPOK00001`) and appears on the LiveArea as
"vitapoke (Pokemon Platinum)".

**2. Launch it once.** It creates `ux0:data/vitapoke/` itself, writes `log.txt`, and stops, because
there is no save file yet.

**3. Put your save in that folder.** Exactly this name, directly in `ux0:data/vitapoke/`:

| Full path on the card | What it is |
|---|---|
| `ux0:data/vitapoke/Platinum.sav` | a 512 KB DS save. `python3 scripts/make_save.py Platinum.sav` writes a blank one; an existing DS save from an emulator or a cartridge dump works too |
| `ux0:data/vitapoke/log.txt` | written by the port, not by you. This is what to send with a bug report |
| `ux0:data/vitapoke/Platinum.nds` | **only if you built without a ROM**: your own dump, which the port then reads from here. A VPK built with one needs nothing here |

The save file has to already exist and be exactly 512 KB. The port will not create one or resize
what is there, so that nothing else at that path can be overwritten by a wrong guess at the format.

**4. Launch it again.** Its first log line says where the game's data is coming from — `[ROMFS] 462
files bundled with this build` for a VPK built with a ROM. If anything is wrong, `log.txt` says
which file it could not open and why, rather than showing a black screen; copy it off the card the
same way you put the save on.

It has to be `ux0` — the memory card (or the internal storage on a PCH-2000, or `uma0` re-mounted as
`ux0`, whichever your setup calls `ux0`). A Vita application cannot write next to its own executable:
`ux0:app` is mounted read only while the application runs, which is why the save and the log live in
a directory under `ux0:data` that you own instead. The game's data is read only, so that can and does
live next to the executable.

## Using a VitaSDK you already have

The download exists so that a fresh clone builds with one command and everyone gets the same
compiler. If you already have VitaSDK installed, point `VITASDK` at it and the build uses that
instead:

```sh
export VITASDK=/usr/local/vitasdk      # the directory with bin/arm-vita-eabi-gcc in it
./build.sh setup                       # names the toolchain, lists what it will install, asks
./build.sh game
```

`VITASDK` is the same variable VitaSDK's own instructions tell you to export, so if you have used it
before, this probably works with no change at all. Keep it set for every `./build.sh` command in that
shell: the build decides which toolchain to use from the environment each time, and a `game` build
that finds a different toolchain than last time starts over.

**What `./build.sh setup` writes into your install.** The renderer needs three libraries at pinned
revisions, and libntr needs some SDL2 declarations that VitaSDK does not ship. So setup installs,
under your `$VITASDK`:

| What | Where | Why |
|---|---|---|
| `libvitaGL.a`, `vitaGL.h` | `arm-vita-eabi/lib`, `.../include` | the renderer draws through it (pinned in `third_party.lock`) |
| `libvitashark.a`, `vitashark.h` | same | vitaGL compiles its shaders on the console through it |
| `libmathneon.a` | same | vitaGL needs it |
| `shacccg_ext.h` | `arm-vita-eabi/include` | one declaration VitaSDK does not ship, so vitaShaRK builds |
| `SDL2/*.h` | `arm-vita-eabi/include/SDL2` | the declarations libntr refers to; shims, not real SDL2 |

It **replaces** any copies of those already there, at the revisions in `third_party.lock`. It lists
all of it and asks before writing anything — answer no and nothing has been touched.
`VITAPOKE_ASSUME_YES=1` answers yes, for a script or a CI job, and a run with nothing to do says so
and writes nothing (two small stamp files, `.vitapoke-deps` and `.vitapoke-shims`, are how it knows).
Nothing else in your install is touched, and the rest of the build writes only inside the project
folder.

If you would rather keep your own copies of those libraries,
`VITAPOKE_SKIP_DEPS=1 ./build.sh setup` builds none of them. It still installs the SDL2 shims and
`shacccg_ext.h`, because nothing compiles without those, and it leaves your `libvitaGL.a`,
`libvitashark.a` and `libmathneon.a` — and their headers — exactly as they are. Then run
`./build.sh check`: it compiles a program against every vitaGL feature the renderer uses, so a
vitaGL that is too old or built differently fails there, with the missing feature named, rather than
the game failing later.

If your platform has no pinned snapshot (`./build.sh setup` says so and stops), this is the way to
build: install VitaSDK however your system prefers and set `VITASDK`.

## Testing

`./build.sh check` compiles and links the platform layer and the GPU features the renderer needs — a
contract check, in seconds.

`./build.sh boot` is the one to run after a build. It installs the VPK into
[Vita3K](https://vita3k.org), starts it, and prints what the port wrote to
`ux0:data/vitapoke/log.txt` -- which is how far it got and why it stopped. With
`--rom your-dump.nds` it uses your ROM; without one, `tests/vita/make_probe_rom.py` writes a file
shaped like a DS ROM (a header and empty tables, no game data) that is enough to test everything
before the game's first data read. `--press 120:Return,150:x` works the buttons on a schedule, which
is as close to playing as an unattended run gets, and `--shacccg libshacccg.suprx` installs the
shader compiler so the GPU path can run at all.

`./build.sh emu-check` is the other one. It builds a test into a VPK, boots it in the
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

- The Vita's buttons map to the DS buttons (○ = A, ✕ = B, △ = X, □ = Y, L, R, START, SELECT, D-pad).
  Unlike the PSP build, **R keeps its DS meaning**: there is no screen-swap mode to bind it to.
- The DS touch screen is **touched**. `TP_*` reads the front panel through `sceTouch`, so there is no
  cursor and no stylus mode — which is also why the game's own name-entry screen is kept, where the
  PSP build had to substitute the system keyboard.
- Both DS screens are drawn at 480x360 — exactly the DS's 4:3 and half the display each — side by
  side, with the touch screen on the right. Neither screen is cropped or covered.

Saves are normal 512 KB DS saves, movable to and from a DS emulator or cartridge dump.

## Contributing

The port is early, and the open pieces are large and fairly independent:

- **running it on a console** — everything here was verified in Vita3K, whose GPU, timing and
  scheduling are approximations; nothing has touched real hardware,
- **the 3D path in a field or battle scene** — it rasterises, but the scenes reached so far are
  mostly 2D with a 3D layer behind them, so the depth convention is still unconfirmed
  (`port/native-vita-render/g3_backend.cpp`),
- **sound out of a speaker** — the mixer produces real samples and `sceAudioOut` takes them; nobody
  has heard them,
- **performance** — the DS's 2D is composed on the CPU, and nothing has been measured on hardware;
  NEON in the 2D compositor and the sound mixer is the obvious next step,
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
4. The DS's two 2D engines are composed in software by vitapoke's renderer (derived from melonDS), and
   its 3D output is rasterised by the Vita's GPU through vitaGL. `docs/VITA.md` explains why that
   division, and why not GXM directly.
5. Everything is linked into a VPK, with each DS overlay module's data laid out so the game can
   "load" a module and get its static data freshly initialised, as the DS did.

The game's data files (graphics, maps, text, sound) are unpacked from your ROM when you build and
packed into the VPK, so at runtime the game opens them by name from inside the application, as it
opened them from a cartridge. Build without a ROM and it reads them from one on the memory card
instead.

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
