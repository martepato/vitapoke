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

The toolchain and source downloads live under `.cache/` in the project folder. To share them between checkouts or
keep them somewhere else, set `VITAPOKE_CACHE=/path/to/cache`.
