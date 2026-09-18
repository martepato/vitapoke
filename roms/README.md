# roms/

Put your own ROM dump here as `Platinum.nds` and `./build.sh game` will unpack it into the VPK, so
that the console needs nothing but the application installed. Nothing here is committed except this
file.

    roms/Platinum.nds        your own dump, used automatically

`./build.sh game --rom /some/other/path.nds` uses a ROM somewhere else instead. With no ROM in either
place the build still works: the VPK then carries no game data and reads the ROM from
`ux0:data/vitapoke/Platinum.nds` on the memory card at run time.

What gets unpacked, and why it is not simply the ROM: the game asks its file system for data by
name, so `scripts/extract_assets.py` writes out the 340 files of the ROM's own file system at their
own paths, plus a 14 KB `nitrofs.idx` holding the cartridge header, the ARM9 overlay table and every
file's path. The ARM code is already compiled into the executable, so none of the ROM's code is
carried. See docs/VITA.md.
