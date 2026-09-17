# Optional LiveArea art

A Vita application shows its own icon and background on the LiveArea screen. Put your own images here
and the build will pack them into the VPK (they are not committed to git):

```
art/platinum/icon0.png           128x128  the application's icon
art/platinum/bg.png              840x500  LiveArea background
art/platinum/startup.png         280x158  the button image
art/soulsilver/icon0.png         128x128
art/soulsilver/bg.png            840x500
art/soulsilver/startup.png       280x158
```

Use plain (non-interlaced) PNGs. Any of them can be left out, and a default is used instead.

**Nothing reads these yet:** VPK packaging is part of the link step, which is not written. See
[docs/VITA.md](../docs/VITA.md).
