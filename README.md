# RPStrudel

Live coding music in the style of [Strudel](https://strudel.cc/) for the
[Picocomputer 6502](https://picocomputer.github.io/), played on its
built-in OPL2. Written in C with [llvm-mos](https://llvm-mos.org/).

<!-- rp6502
preset: llvm-mos/Release
publish: RPStrudel.zip
frames: 300
-->
[![RPStrudel](https://jasonfrowe.github.io/RPStrudel/RPStrudel/screenshot.png)](https://jasonfrowe.github.io/RPStrudel/RPStrudel/)

[Run it in your browser](https://jasonfrowe.github.io/RPStrudel/RPStrudel/).

## Status

Milestone 4: most of Strudel's everyday functions, on top of the live
coding editor (milestone 3). Edit the code, press Ctrl+Enter, and the
change plays from the next cycle. If the new code has an error, the old
code keeps playing and the error is marked in red.

```js
setcpm(30)
$: s("bd(3,8,<0 2>), [~ sd]*2").every(4, x => x.ply(2))
$: s("hh*8?").gain("[.5 1]*4")
$: n("<0 3 5 [7 10]>*2").scale("C2:minor").s("gm_synth_bass_1")
$: n("0 [2 4] <3 6> 7").scale("C4:minor").s("gm_epiano1")
  .off(0.25, x => x.add(note(12))).gain(.6)
```

**Mini-notation**: sequences, `~` rests, `[ ]`, `< >`, `,` stacks, `*`
and `/` (also with patterns, as `*<2 3>`), `!`, `@`, `_`, euclid `(3,8,2)`
(patterns allowed), `.` feet, `{ }%n` polymeter, `bd:2`, `?` and `|`.
Not yet: `..`.

**Program**: `$:` patterns (`_$:` mutes one), `setcpm()`, `setcps()`,
`//` comments, and these functions:

| | |
|---|---|
| Sound | `s`/`sound`, `note`, `n`, `gain` (also chained, and as `"c e".note()`) |
| Time | `fast`, `slow`, `rev`, `late`, `early`, `ply` |
| Changing | `every`/`firstOf`, `lastOf`, `off`, `echo` |
| Values | `add`, `sub`, `scale` (major, minor, the modes, harmonic and melodic minor, pentatonic, minor pentatonic, blues, chromatic, whole tone) |
| Structure | `struct`, `mask`, `euclid`, `euclidRot` |
| Random | `degrade`, `degradeBy`, `sometimes`, `sometimesBy`, `often`, `rarely`, `almostNever`, `almostAlways` |
| Combining | `stack`, `cat`/`slowcat`, `seq`/`fastcat`, `silence` |

Functions given to others can be lambdas (`x => x.fast(2)`), calls
(`fast(2)`) or names (`rev`). As in Strudel, `add` on a sound or note
pattern needs `note(...)` or `n(...)`: `.add(note(12))`.

**Sounds**: `bd`, `sd`, `hh`, `sine`, `triangle` (the default), `square`,
`sawtooth`, and Strudel's General MIDI names (`gm_piano`,
`gm_synth_bass_1`, ...), from RPTracker's OPL2 patches.

Each playing token lights up in the code, as in Strudel, and the
highlights follow the text as you edit.

### How it matches Strudel

The engine mirrors Strudel's own building blocks (`pure`, `stack`,
`slowcat`, `fast`, `compress`/`fastGap`, `repeatCycles`, `struct`,
`appLeft`, `squeezeJoin`...) with exact fractions. `tests/ref` runs
Strudel itself (from npm) to make the expected results, and the host tests
check that RPStrudel gives the same events for 78 mini-notation cases and
57 program cases.

The one deliberate difference is randomness: `?`, `|`, `degradeBy` and
`sometimes` use RPStrudel's own random numbers, which repeat every 16
cycles (that keeps random patterns as cheap as any other), so they choose
differently from Strudel.

### Fitting a 6502

- Time is 16-bit fractions. Each pattern's period (when it repeats) is
  worked out when it's parsed, and it's queried at `cycle mod period`, so
  the numbers stay small however long it plays.
- Notes for each part of a pattern's period are cached, so after the
  first time through, a cycle takes ~30 ms instead of ~400 ms.
- **Overlays**: the parser (15K) and the queries (24K) are never needed at
  the same time, so they share one region of RAM (`src/rpstrudel.ld`,
  `src/overlay.h`). Each is a file in the ROM (`parse.ovl`, `query.ovl`,
  copied out of the ELF by the build), read in when needed: the parser for
  a Ctrl+Enter, then the queries again (~190 ms in all).
- In XRAM: the patches and sound names, the code text (an 8K gap
  buffer), the patterns (two banks, so a new program is parsed beside the
  playing one), the note cache, and the clock's event buffers.
- Built with `-Oz`.

The screen shows the timer rate, the time to work out each cycle, how
much of the stack was never used, and how many cycles were late (played
again because the next one wasn't ready).

## Controls

| Key | Action |
|---|---|
| Ctrl+Enter (or Alt+Enter) | Play, or update to the edited code |
| Ctrl+. (or Alt+.), Esc | Stop |
| Arrows, Home, End, PgUp, PgDn | Move |
| Backspace, Delete, Tab, Enter | Edit (Enter keeps the indent) |
| Ctrl+K | Delete the line |
| Ctrl+S, Ctrl+O | Save, open (`.str` and `.txt` files on the USB drive) |
| Ctrl+D | Write the screen's text to the console (for test scripts) |

Typing comes from the console, so the keyboard layout and key repeat are
the firmware's. The timer interrupt watches the shortcut keys, so a press
isn't missed while a cycle is being worked out.

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
./tools/rp6502-emu build/llvm-mos/release/RPStrudel.rp6502 --mute --script tests/editor.script
./tools/rp6502-emu build/llvm-mos/release/RPStrudel.rp6502 --mute --script tests/keys.script
./tools/rp6502-emu build/llvm-mos/release/RPStrudel.rp6502 --mute --script tests/functions.script
```

`tests/editor.script` types code, presses Ctrl+Enter, and checks the
screen through Ctrl+D: an update, a mistake that leaves the old code
playing, a fix with the editing keys, an unknown sound, and Esc.
`tests/keys.script` sends arrow sequences split across frames (as real
hardware can) and holds an arrow key down, checking that no cycle is late.
`tests/functions.script` types code with the milestone 4 functions, and an
unknown scale.

The host tests also check the random functions (`tests/random_cases.txt`):
how many events they keep, and that they repeat every 16 cycles.

To add a case, put it in `tests/mini_cases.txt` or `tests/prog_cases.txt`
and remake the expected results with Strudel:

```sh
cd tests/ref && npm install && node --import ./stub.mjs gen.mjs
```

## Layout

| File | What it does |
|---|---|
| `src/rat.c` | 16-bit exact fractions |
| `src/pool.c` | Where the patterns live: XRAM, or an array on the Mac |
| `src/pattern.c` | Pattern nodes and values in the pool, two banks |
| `src/query.c` | Queries (Strudel's primitives), periods, randomness (overlay) |
| `src/scales.c` | Scale names and notes |
| `src/mini.c` | Mini-notation parser (overlay) |
| `src/prog.c` | Program parser: `$:` lines, functions, lambdas (overlay) |
| `src/overlay.c` | Reads an overlay in from the ROM |
| `src/rpstrudel.ld` | Linker script with the overlay region |
| `src/player.c` | Events to OPL2 notes: sounds, voices, note-offs, cache |
| `src/clock.c` | VIA timer interrupt, cycle phase, event buffers |
| `src/opl.c` | OPL2 registers (XRAM portal 1, interrupt only) |
| `src/patches.c` | Patches and sound names in XRAM |
| `src/screen.c` | Mode 1 text drawing (XRAM portal 0) |
| `src/editbuf.c` | The code text: a gap buffer in XRAM |
| `src/editor.c` | Editing keys, scrolling, drawing with colours |
| `src/main.c` | Screen layout, Ctrl+Enter, highlights, main loop |
| `tools/patches.py` | The OPL2 patch bank and sound names |
| `tests/host` | Host tests and a benchmark of fraction operations |
| `tests/ref` | Runs Strudel to make the expected results |
