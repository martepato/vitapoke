# Tests

## Vita checks

`tests/vita/run.sh` (also `./build.sh check`) needs no ROM, no emulator and no game build. It compiles
and links two translation units against the pinned VitaSDK:

- `ds_surface.c` calls every DS interface the Vita platform layer in `port/vita` implements -- the
  arena, the tick clock, interrupts, threads, alarms, the touch panel and the clock -- and links it.
  A psp2 or libntr rename after a toolchain bump fails here instead of several phases into a build. It
  defines the DS hardware registers itself, because the real build generates those from the
  decompilation.
- `vitagl_features.c` uses the three GPU features the renderer design depends on -- paletted textures,
  stencil and render-to-texture -- so losing one of them is a failed check rather than a surprise
  later.

Neither runs; both are contract checks.

`tests/vita/vita3k.sh` (also `./build.sh emu-check`) goes further and *runs* the platform layer, by
building `ds_runtime.c` into a VPK, booting it in the Vita3K emulator and reading back the report it
writes to `ux0:data`. It downloads the emulator (about 65 MB) and takes a couple of minutes, so it is
not part of `run.sh`.

It checks the things a compiler cannot: that the DS execution lock really serialises DS threads (with
an unlocked control run to prove the test can fail), that a wake is not lost between a thread queueing
itself and sleeping, that alarms fire and cancel, and that the psp2 semantics the design depends on are
what the port assumes. Vita3K needs a display (Xvfb is fine), an OpenGL driver (llvmpipe is fine), and
refuses to run as root; the script handles or reports each of those.

See [docs/VITA.md](../docs/VITA.md).

## Fixtures

`fixtures/` holds synthetic saves made in an emulator -- no ROM data. They were the starting states for
a scenario suite that replayed recorded input in a PSP emulator; that suite went with the PSP target,
and an equivalent driven through Vita3K is open work. The saves are kept because they will be the
starting states for it too.

## What is missing

There is no end-to-end test, because nothing links yet. Once a VPK boots, the scenarios worth rebuilding
first are the ones that caught real bugs before: a new game reaching the name-entry keyboard, a battle,
an evolution, and each quality-of-life change.
