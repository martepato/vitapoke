# pspoke tests

`tests/run.sh` is the regression suite. It builds each game with `./build.sh`, checks the EBOOT against the PSP
loader limits, and then, if you have headless PPSSPP, replays recorded scenarios and reads the game's own log for
traps, missing overlays and the events each scenario is expected to reach.

```sh
# build + loader audit only (no emulator needed)
tests/run.sh --platinum-rom "path/to/Platinum.nds" --soulsilver-rom "path/to/SoulSilver.nds"

# with the emulator scenarios (about 25 minutes for everything, 5 for --quick); add --platinum-save <copy.sav>
# for the Platinum overworld scenarios (see below)
export PPSSPP_HEADLESS=/path/to/PPSSPPHeadless
tests/run.sh --soulsilver-rom "path/to/SoulSilver.nds"                 # every SoulSilver scenario
tests/run.sh --soulsilver-rom "path/to/SoulSilver.nds" --quick         # smoke scenarios only
tests/run.sh --soulsilver-rom "path/to/SoulSilver.nds" --only catch,qol-repel-yes
tests/run.sh --list
```

Results go to `.work/tests/`: `summary.txt`, and for each scenario `<name>.log` (PPSSPP log with the game's
`native-memlog.txt` appended) and `<name>.png` (the last frame). The runner ends by relinking the normal build, so
`dist/` is left as `./build.sh` produces it. `--skip-build` reuses the existing `.work` tree without rebuilding first.

Run this before opening a pull request. The QoL scenarios (`qol-*`) expect the default build switches; they fail on
purpose on a `--no-trade-evos` or `--no-repel-prompt` build.

## Headless PPSSPP

The scenarios need `PPSSPPHeadless`, PPSSPP's command-line build. It is not in the release downloads, so build it
from source (about 10 minutes):

```sh
git clone --recurse-submodules https://github.com/hrydgard/ppsspp.git
cd ppsspp && cmake -B build -DHEADLESS=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
export PPSSPP_HEADLESS=$PWD/build/PPSSPPHeadless
```

Timings in the emulator say nothing about the real PSP (headless runs about six times faster than the hardware),
and PPSSPP does not enforce the 32 MiB loader limit; the loader audit does. The suite passing is not a substitute
for trying a build on a PSP.

## How a scenario works

- `tests/fixtures/soulsilver/*.sav` are 512 KiB save files created by playing the game in the emulator (no ROM data,
  not anyone's real save). The runner copies them into the throwaway memory stick that
  `port/soulsilver-native-core/nitromain-perf/run_probe.py` stages; your own saves are never read.
- `port/soulsilver-native-core/nitromain-perf/input-*.txt` are recorded inputs: one line per change,
  `frame buttons-hex x y` (`0x2000` A, `0x4000` B, `0x10` up, `0x40` down, `0x80` left, `0x20` right, `0x1000`
  Start, `0x8` Select, `0x200` R; frames strictly increasing). `port/soulsilver-native-play/maploader/gen_script.py`
  writes one from short tokens (`A`, `W300`, `U8`, `a12` = tap A 12 times).
- The test relink passes `PSP_NATIVE_PROBE_FRAMES` so `probe-frame-limit.txt` in the stage folder ends the game
  after the scenario's last input plus a margin, printing `[NATIVE-FRAME] clean bounded probe exit`.
- Some scenarios relink with diagnostic make variables from the SoulSilver Makefile (`WARP_TO`, `NO_WILD`,
  `GIVE_SPECIES`, `REPEL_STEPS`, ...) that set the scene up on the copied save. These wrap game functions at link
  time and never exist in a normal build.
- A scenario fails when the log has a trap line (`[FATAL]`, `GF_ASSERT`, `SS-MISSING`, an invalid address), no
  clean exit, or misses its expected events (`overlay:<id>` = the game loaded that DS overlay, e.g. 12 = battle,
  15 = evolution; `log:<text>` = a line the port prints).

## Scenarios

| Name | What it covers |
|---|---|
| `boot` (Platinum) | Blank save through the title and intro for 60 s of game time; sound engine ready, no traps. |
| `oreburgh-belts` (Platinum, needs `--platinum-save`) | Warps to the south end of Oreburgh City and walks toward the Mine; the long conveyor belts (bounding boxes larger than the view) must still be drawn ten steps in. Caught the box-test bug. |
| `floaroma-gate` (Platinum, needs `--platinum-save`) | Warps to Floaroma Town's south gate. Its translucent arch and shade are submitted before the ground; drawn in submission order they were a solid black block. Screenshot check by eye (`.work/tests/floaroma-gate.png`). |
| `smoke` | Violet City: continue a save and walk around. |
| `pc`, `easychat`, `pokedex`, `apricorn`, `vs-recorder`, `trainer-card`, `options*` | Menus and sub-applications, each from the same Violet City save; the `options-*` variants leave the Options screen every possible way. |
| `catch` | Route 31 wild battle and a catch (battle overlay 12); also checks the field-move buffs are in the loaded move table. |
| `geonet` | Pokégear globe (the 1024x512 texture that must be downscaled; overlay 69). |
| `gym-pryce` | Mahogany Gym battle at Lv100 with particle-heavy moves. |
| `rocket-radio-tower` | Team Rocket grunt sight line in the Radio Tower (overlay 117) into a battle. |
| `qol-friendship-evo`, `qol-trade-item-evo`, `qol-trade-level-evo` | The trade-evolution replacements (Chingling, Onix + Metal Coat, Kadabra at Lv36 reach the evolution screen, overlay 15). |
| `qol-repel-yes`, `qol-repel-no` | The repel re-use prompt, both answers. |

## Adding a scenario

1. Pick or make a fixture: play to the spot in the emulator (`run_probe.py` keeps the synthetic save in its run
   folder) and copy the `.sav` into `tests/fixtures/<game>/`. Keep it synthetic: never a save with someone's real
   progress.
2. Record the inputs (`gen_script.py` or by hand) into `port/soulsilver-native-core/nitromain-perf/input-<name>.txt`,
   and check the run's `screen.png` shows what you meant.
3. Add an `ss_case` line to `tests/run.sh` with the events the log must contain, and list it here.

## Platinum scenarios and `--platinum-save`

Platinum has no committed save fixture yet, so scenarios that need the overworld take a save you supply:
`--platinum-save path/to/copy.sav` (or `PLATINUM_SAVE=...`). Only a copy is ever staged; the file is never modified.
Any save that continues into the overworld works, because the scenario warps where it needs to go with the
`WARP_TO=<mapHeaderId>,<x>,<z>` diagnostic (`port/native-audio-app/diag_warp.c`; map ids are the decompilation's
`MAP_HEADER_*` enum values, e.g. Oreburgh City 45, Oreburgh Mine B1F 198). Platinum inputs are compiled in from
`tests/platinum/*.h` (`struct ScriptEvent{first,last,bits,x,y,down}`, PSP button bits, optional touch point).
Without `--platinum-save` these scenarios are reported as skipped.

## Not covered yet

- A committed Platinum fixture: the old scripted new-game replay (`input_script.h`) predates the instant-text
  change and no longer lines up with the intro. Recording a new one (the intro needs touch presses, which the
  script format supports) and saving a synthetic fixture from it would let the Platinum scenarios run without
  `--platinum-save`.
- Nothing here measures performance or sound on the real PSP; see `docs/DEVELOPING.md` for the `--dev` build's
  on-card logging.

## Vita port checks

`tests/vita/run.sh` (also `./build-vita.sh check`) is separate from the suite above and needs no ROM,
no emulator and no PSP toolchain. It compiles and links two translation units against the pinned
VitaSDK:

- `ds_surface.c` calls every DS interface the Vita platform layer in `port/vita` implements -- the
  arena, the tick clock, interrupts, threads, alarms, the touch panel and the clock -- and links it.
  A psp2 or libntr rename after a bump fails here instead of several phases into a build. It defines
  the DS hardware registers itself, because the real build generates those from the decompilation.
- `vitagl_features.c` uses the three GPU features the renderer design depends on -- paletted textures,
  stencil and render-to-texture -- so losing one of them is a failed check rather than a surprise
  later.

Neither runs; both are contract checks. See [docs/VITA.md](../docs/VITA.md).

`tests/vita/vita3k.sh` (also `./build-vita.sh emu-check`) goes further and *runs* the platform layer,
by building `ds_runtime.c` into a VPK, booting it in the Vita3K emulator and reading back the report it
writes to `ux0:data`. It downloads the emulator (about 65 MB) and takes a couple of minutes, so it is
not part of `run.sh`.

It checks the things a compiler cannot: that the DS execution lock really serialises DS threads (with
an unlocked control run to prove the test can fail), that a wake is not lost between a thread queueing
itself and sleeping, that alarms fire and cancel, and that the psp2 semantics the design depends on are
what the port assumes. Vita3K needs a display (Xvfb is fine), an OpenGL driver (llvmpipe is fine), and
refuses to run as root; the script handles or reports each of those.
