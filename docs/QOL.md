# Quality of Life changes

pspoke changes a few things about how Platinum and SoulSilver play. Every change is **on by default**, has a
name, and can be left out at build time:

| Change | Build flag to turn it off | Platinum | SoulSilver |
|---|---|---|---|
| [Instant text](#instant-text) | `--no-instant-text` | yes | yes |
| [Trade evolutions without trading](#trade-evolutions-without-trading) | `--no-trade-evos` | yes | yes |
| [Repel re-use prompt](#repel-re-use-prompt) | `--no-repel-prompt` | yes | yes |
| [HM moves can be forgotten](#hm-moves-can-be-forgotten) | `--no-forget-hms` | yes | yes |
| [Field-move buffs](#field-move-buffs) | `--no-move-buffs` | yes | yes |

```sh
./build.sh soulsilver --rom "path/to/SoulSilver.nds" --no-trade-evos                 # one change off
./build.sh platinum   --rom "path/to/Platinum.nds" --no-instant-text --no-trade-evos --no-repel-prompt --no-forget-hms --no-move-buffs   # the original game
```

The flags only change code that runs in RAM: your ROM is never modified and the save format is the same, so a save
moves freely between builds with different switches (a Pokémon that evolved under `--trade-evos` stays evolved).
The switches are `#define`s in a generated header (`pspoke_qol.h`), and every change in the source is wrapped in
`#if VITAPOKE_QOL_...` so you can find them: `patches/platinum/qol-overlay-sources.patch` and
`patches/soulsilver/qol.patch`.

## Instant text

Dialogue prints a whole page at once at every text-speed setting, including the default. Everything else about
messages is unchanged: button prompts, page scrolling, timed pauses and the "..." beats still wait as before, so
cutscenes keep their timing. This is the change most people will notice most, and the one to turn off first if you
want the game exactly as it was.

## Trade evolutions without trading

There is no link cable or Wi-Fi here, so the Pokémon that only evolve by trading could never evolve. The rules below
follow Drayano's Renegade Platinum (Platinum) and Sacred Gold / Storm Silver (SoulSilver) documentation where the
ROM has the items to support it. Nothing else about evolution changes.

### Level 36 instead of a trade

| Pokémon | Evolves into | Was | Now |
|---|---|---|---|
| Kadabra | Alakazam | trade | level 36 |
| Machoke | Machamp | trade | level 36 |
| Graveler | Golem | trade | level 36 |
| Haunter | Gengar | trade | level 36 |

### Use the held item from the Bag instead of a trade

Pick the item in the Bag and use it on the Pokémon, exactly like an evolution stone. The item is consumed.

| Pokémon | Evolves into | Item |
|---|---|---|
| Onix | Steelix | Metal Coat |
| Scyther | Scizor | Metal Coat |
| Seadra | Kingdra | Dragon Scale |
| Poliwhirl | Politoed | King's Rock |
| Slowpoke | Slowking | King's Rock |
| Porygon | Porygon2 | Up-Grade |
| Porygon2 | Porygon-Z | Dubious Disc |
| Rhydon | Rhyperior | Protector |
| Electabuzz | Electivire | Electirizer |
| Magmar | Magmortar | Magmarizer |
| Dusclops | Dusknoir | Reaper Cloth |
| Clamperl | Huntail | DeepSeaTooth |
| Clamperl | Gorebyss | DeepSeaScale |

### Happiness evolutions at any time of day

Budew, Chingling and Riolu evolve from happiness whether it is day or night (originally they needed daytime).

### Platinum only

- Eevee evolves with stones instead of by happiness or location: **Sun Stone → Espeon**, **Moon Stone → Umbreon**,
  **Leaf Stone → Leafeon**. Happiness by day/night and the Moss Rock no longer evolve Eevee in this build.
  Glaceon still needs the Ice Rock (Platinum has no Ice Stone), and Feebas keeps its Beauty evolution (no Prism Scale).
- The friendship babies start with base happiness 180 instead of 70, so they evolve much sooner: Pichu, Cleffa,
  Igglybuff, Togepi, Azurill, Budew, Chingling, Happiny. (Applies to Pokémon obtained on this build; an existing
  Pokémon keeps the happiness it has.)

### SoulSilver only

- Eevee is unchanged: happiness by day → Espeon, by night → Umbreon, Moss Rock → Leafeon, Ice Rock → Glaceon.
- No base-happiness change.

## Repel re-use prompt

When a Repel wears off and the Bag has another of the same kind, the game asks "Use another?" (as Black/White and
later games do). Yes uses one and starts a new effect; No leaves the Bag alone. The message is added to the existing
"Repel's effect wore off..." script; nothing else about Repels changes.

## HM moves can be forgotten

A Pokémon can forget an HM move the same way as any other move: in the summary screen, when learning a new move
on level-up or from a TM, and in battle. No Move Deleter visit needed. Teaching an HM still does not use up the
HM. (On SoulSilver the Bag's "Booted up an HM!" line now reads "Booted up a TM!"; cosmetic.)

## Field-move buffs

Applied to the move table when it is read from your ROM (the ROM is untouched):

| Move | Was | Now |
|---|---|---|
| Cut | 50 power, 95% accuracy | 60 power, 100% accuracy |
| Rock Smash | 40 power | 60 power |
| Whirlpool | 15 power, 70% accuracy | 35 power, 85% accuracy (Black/White values) |

Strength, Surf, Waterfall, Fly, Rock Climb, Defog and Flash are unchanged. These are pspoke's own numbers, chosen
so the early HMs are not a wasted move slot; edit them in `PSPQoL_ApplyMoveBuffs` (both patches) if you prefer
different values.

## Reporting a problem with a QoL change

If something misbehaves, try the build with that change turned off; if the problem goes away, say so in the issue.
The regression suite (`tests/README.md`) covers the Chingling, Onix and Kadabra evolutions (the Kadabra one goes
through the forget-a-move summary screen), both answers to the repel prompt, and the move buffs on SoulSilver.
