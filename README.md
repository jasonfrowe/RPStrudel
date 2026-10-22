# RPStrudel

Live coding music in the style of [Strudel](https://strudel.cc/) for the
[Picocomputer 6502](https://picocomputer.github.io/), played on its
built-in OPL2. Written in C with [llvm-mos](https://llvm-mos.org/).

## Status

Milestone 2: Strudel's pattern engine on the 6502. The program is fixed
for now (in `src/main.c`); the editor comes in milestone 3.

```js
setcpm(30)
$: s("bd(3,8), [~ sd]*2")
$: s("hh*8").gain("[.5 1]*4")
$: note("<[c2 c3]*4 [ab1 ab2]*4>").s("gm_synth_bass_1")
$: note("<c4 eb4 g4 [bb4 c5]>(3,8)").s("gm_epiano1").gain(.7)
```

**Mini-notation**: sequences, `~` rests, `[ ]`, `< >`, `,` stacks, `*`
and `/` (also with patterns, as `*<2 3>`), `!`, `@`, `_`, euclid `(3,8,2)`
(patterns allowed), `.` feet, `{ }%n` polymeter, `bd:2`. Not yet: `?` and
`|` (random) and `..`.

**Program**: `$:` patterns (`_$:` mutes one), `setcpm()`, `setcps()`,
`s`/`sound`, `note`, `n`, `gain` (also chained, and as `"c e".note()`),
`fast`, `slow`, `//` comments.

**Sounds**: `bd`, `sd`, `hh`, `sine`, `triangle` (the default), `square`,
`sawtooth`, and Strudel's General MIDI names (`gm_piano`,
`gm_synth_bass_1`, ...), from RPTracker's OPL2 patches.

Each playing token lights up in the code, as in Strudel.

### How it matches Strudel

The engine mirrors Strudel's own building blocks (`pure`, `stack`,
`slowcat`, `fast`, `compress`/`fastGap`, `repeatCycles`, `struct`,
`appLeft`) with exact fractions. `tests/ref` runs Strudel itself (from npm)
to make the expected results, and the host tests check that RPStrudel
gives the same events for 78 mini-notation cases and 19 program cases.

### Fitting a 6502

- Time is 16-bit fractions. Each pattern's period (when it repeats) is
  worked out when it's parsed, and it's queried at `cycle mod period`, so
  the numbers stay small however long it plays.
- Notes for each part of a pattern's period are cached, so after the
  first time through, a cycle takes ~30 ms instead of ~400 ms.
- Patches and sound names live in XRAM (`tools/patches.py` makes them).
- Built with `-Oz`: the code is ~45K, and RAM is the limit.

The screen shows the timer rate, the time to work out each cycle, and how
much of the stack was never used.

## Controls

| Key | Action |
|---|---|
| Space | Play / stop |
| `-` / `=` | Tempo down / up, in cycles per minute |
| Esc | Hush |

## Build

```sh
cmake --preset llvm-mos/Release
cmake --build --preset llvm-mos/Release
./tools/rp6502-emu build/llvm-mos/release/RPStrudel.rp6502
```

## Tests

```sh
make -C tests/host        # pattern engine vs Strudel, on the Mac
./tools/rp6502-emu build/llvm-mos/release/RPStrudel.rp6502 --mute --script tests/smoke.script
```

To add a case, put it in `tests/mini_cases.txt` or `tests/prog_cases.txt`
and remake the expected results with Strudel:

```sh
cd tests/ref && npm install && node --import ./stub.mjs gen.mjs
```

## Layout

| File | What it does |
|---|---|
| `src/rat.c` | 16-bit exact fractions |
| `src/pattern.c` | Pattern nodes and queries (Strudel's primitives), periods |
| `src/mini.c` | Mini-notation parser |
| `src/prog.c` | Program parser (`$:` lines, functions, method chains) |
| `src/player.c` | Events to OPL2 notes: sounds, voices, note-offs, cache |
| `src/clock.c` | VIA timer interrupt, cycle phase, event buffers |
| `src/opl.c` | OPL2 registers (XRAM portal 1, interrupt only) |
| `src/patches.c` | Patches and sound names in XRAM |
| `src/screen.c` | Mode 1 text drawing (XRAM portal 0) |
| `src/main.c` | Screen layout, keys, main loop |
| `tools/patches.py` | The OPL2 patch bank and sound names |
| `tests/host` | Host tests and a benchmark of fraction operations |
| `tests/ref` | Runs Strudel to make the expected results |
