# Installation details

Prerequisites: git, python3, make, patch, rsync and curl. `build.sh` checks for them and, if any are missing,
offers to install them for you: on Linux through apt, dnf, pacman or zypper (with `sudo`, after asking; set
`VITAPOKE_ASSUME_YES=1` to skip the question), and on macOS by opening the installer for Apple's command line
tools, which include all of them. To install them yourself instead:

- macOS: `xcode-select --install`
- Ubuntu/Debian: `sudo apt install git python3 make patch rsync curl`
- Fedora: `sudo dnf install git python3 make patch rsync curl`
- Windows: use WSL2 (Ubuntu) and follow the Linux steps. Clone inside the WSL Linux filesystem (not `/mnt/c`) so the
  scripts keep LF line endings. Untested on Windows.

`./build.sh setup` needs only git, make, curl and tar; the extra tools are for building a game.
`./build.sh emu-check` additionally needs a display or `xvfb-run`, an OpenGL driver, and `unzip`.

Your own ROM dump goes at `roms/Platinum.nds` (see `roms/README.md`). The build unpacks the game's
data out of it and packs that into the VPK, so the console needs nothing but the application
installed; `./build.sh game --rom FILE` uses a ROM somewhere else. Building with no ROM at all works
and is what the checks use — the VPK then carries no game data and reads a ROM from the memory card
at run time instead.

The toolchain and source downloads live under `.cache/` in the project folder. To share them between checkouts or
keep them somewhere else, set `VITAPOKE_CACHE=/path/to/cache`.

## A VitaSDK you already have

`./build.sh setup` downloads a pinned VitaSDK snapshot because that way a fresh clone builds with one
command and everybody's build uses the same compiler. It is not the only way: set `VITASDK` to an
install you already have and nothing is downloaded.

```sh
export VITASDK=/usr/local/vitasdk      # the directory with bin/arm-vita-eabi-gcc in it
./build.sh setup
./build.sh game
```

Keep `VITASDK` set for every `./build.sh` in that shell. A `game` build records which toolchain
compiled the tree, so running one with a different toolchain than last time rebuilds from the start
instead of mixing objects from two compilers — correct, but it costs the full build, so it is worth
setting the variable in your shell profile rather than per command.

`setup` installs three libraries at the revisions in `third_party.lock` into your install, plus two
headers and the SDL2 declarations libntr refers to; it asks first, `VITAPOKE_ASSUME_YES=1` answers
yes, and `VITAPOKE_SKIP_DEPS=1` keeps the libraries you already have. The table of exactly what goes
where is in README.md under "Using a VitaSDK you already have".

Setting `VITASDK` is also the answer if `./build.sh setup` tells you there is no prebuilt snapshot
for your platform: install VitaSDK however your system prefers, then point `VITASDK` at it.
