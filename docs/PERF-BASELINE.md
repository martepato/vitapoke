# Where the frame goes, and how to measure it without a console

This port is tuned against two things: logs from a Vita, and `tests/vita/town.sh`, which drives the
emulator into the overworld and measures a scene that is standing still. The first is the truth. The
second is the one that can be run a dozen times an hour, and this file is what makes its numbers
mean something.

## The loop

```
VITAPOKE_REPORT_FRAMES=150 ./build.sh game       # short report windows, for iterating
tests/vita/town.sh --save your.sav --windows 3   # install a save, drive to town, measure
tests/vita/town.sh --windows 3 --throttle 30     # same, at roughly console speed
```

The save is yours and stays out of the repository: `--save` copies it to `.work/vita3k-save/`, which
is ignored, and `*.sav` is ignored anyway. A Platinum save is 512 KiB and the script refuses
anything else.

Getting to town is done by watching the game's own log for `enter gFieldMapTemplate` rather than by
timing key presses, because the timing is not reproducible. Once it appears the script stops
touching the controls, so the character stands where the save left them and the scene does not
change between windows. Two consecutive windows agreeing on `verts=` and `polygons=` is the sign
that a measurement is worth reading.

## What the emulator can and cannot tell you

Vita3K recompiles the game's ARM code onto a desktop processor, so its microseconds are not the
console's. What carries across:

- **The game thread's shape.** Which `[GPROF]` bucket grew, and by how much, relative to the others.
- **Counters that belong to the game rather than the machine**: `verts=`, `normals=`, `polygons=`,
  `binds=`, `hits=`, `decodes=`. These are what the game sent that frame and are identical on both.
- **Whether a change moved `game_us` at all**, and in which direction.

What does not carry across:

- **The renderer.** In this emulator the 3D layer reads back empty (`layer=N/0`) and `upload_us` and
  `present_us` are a tenth of what a console reports. The GPU half of the frame has to be measured
  on hardware.
- **Absolute frame rate.** Unthrottled, the emulator sits at the game's own 30 fps ceiling whatever
  the port does, which tells you nothing.

## What it cannot price

The emulator recompiles ARM onto a desktop processor, and the two do not agree about what is
expensive. A Cortex-A9's floating-point divide is fifteen to twenty cycles and is not pipelined; the
x86 instruction it becomes is about four and is. So a change that removes float divisions -- and
several of the ones this port has needed are exactly that -- barely moves the number here and moves
it on the console.

Hoisting the perspective divide out of the polygon loop measured 4.6% here while removing about two
fifths of the divides on the path. Both of those are true. When a change is of that kind, read the
instruction counts instead:

```
OBJ=$(find .work/vita -name g3_handler.o | head -1)
arm-vita-eabi-objdump -d "$OBJ" | awk '/^[0-9a-f]+ <G3SIM_Vtx>:/{n=0;g=1} g&&/^ *[0-9a-f]+:/{n++} /^$/{if(g)print n; g=0}'
```

That is the console's arithmetic, counted on the console's instruction set, and it is the honest
measure for this class of change. The emulator's job for those is to prove the output did not
change.

## The throttle, and what it is worth

`--throttle 30` was derived from per-vertex cost and turns out to be too harsh to be useful: at that
quota the run drops to 6 fps, and the reason is `present_us` going from 0.2 ms to 79 ms. That is
Vita3K's own OpenGL being starved of CPU, not anything about the port. A quota throttles the
emulator, and most of what the emulator does is not the game.

So the throttle is worth using only to put the *game thread* under pressure, and only its
`[GPROF]` buckets are worth reading while it is on. For "does this hold 30 fps", the useful sum is
the game thread's own work (`game_us` minus `idle_us`) scaled by the per-vertex ratio below, plus
what a console log says the renderer costs. Standing in town that comes to roughly 30 ms of game
thread and 8 ms of renderer, which is not 33.

## The measurements this is calibrated against

Standing in the overworld, unthrottled, from `tests/vita/town.sh`:

```
fps=30.00  game_us=32334  idle_us=23629   (8.71 ms of real work)
app=7746 self            land_render=6950 self + 498 3D
verts=7529  normals=858  polygons=3839
```

`land_render` is about eighty per cent of the game thread's work. `fieldeff_render` and
`owanim_render` report nearly the same number as each other because one calls the other, so count
one of them, not both.

A Vita, in the field, from a log sent back after the vertical-blank fix:

```
fps=21.41  game_us=38332  idle_us=0  render_us=7510
land_render=29309 self + 5354 3D          polygons=4680
```

The console's window has 4680 polygons to the emulator's 3839. Scaling the emulator's vertex count
by that ratio gives about 9200 vertices for the console's window, so:

```
console   29309 us / ~9200 verts  = ~3.2 us per vertex
emulator   7285 us /  7529 verts  = ~1.0 us per vertex
```

**About 3.3 times**, which is why `--throttle 30` is the default calibration: a CPU quota of 30% of
one core puts the emulator's per-vertex cost in the console's range. It is a share of CPU time
rather than a slower processor -- the mix of work is unchanged and only the rate is -- so it answers
"does this hold 30 fps" without pretending to be a Vita. Re-derive the ratio whenever a hardware log
arrives with `verts=` in it from a scene the emulator can also reach; the estimate above is the
weaker part of this file.

## Reading a frame

`[PERF]` splits the frame into the game thread (`game_us`, which contains `idle_us`) and the
renderer (`render_us`, which contains `upload_us`, `present_us`, `wait_us`, and `other_us` -- the 3D
readback). `compose_us` is the software 2D compositor, which runs on a worker thread and is only on
the critical path when `wait_us` is not near zero.

`[GPROF]` splits the game thread by subsystem, as `self_us/3d_us/calls` per frame. `app` nests the
rest, so it double-counts; read the leaves. `land_render` is `LandDataManager_RenderLoadedMaps`,
which is the map's geometry going through the software geometry engine and is the largest single
cost in the field.

`late=` in `[PERF]` counts vertical blanks the game asked for that had already gone by, which the
port hands back rather than waiting for. In a scene the port cannot keep up with it equals the
number of waits; when it is near zero the port is keeping pace and the frame rate is the game's own.
