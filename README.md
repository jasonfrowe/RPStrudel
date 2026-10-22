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
| Values | `add`, `sub`, `trans`/`transpose` (semitones, or intervals as `3M`, `5P`, `-2m`), `scale` (major, minor, the modes, harmonic and melodic minor, pentatonic, minor pentatonic, blues, chromatic, whole tone; written `"C4:minor"`) |
| Structure | `struct`, `mask`, `euclid`, `euclidRot` |
| Random | `degrade`, `degradeBy`, `sometimes`, `sometimesBy`, `often`, `rarely`, `almostNever`, `almostAlways` |
| Combining | `stack`, `cat`/`slowcat`, `seq`/`fastcat`, `silence` |

Functions given to others can be lambdas (`x => x.fast(2)`), calls
(`fast(2)`) or names (`rev`). As in Strudel, `add` on a sound or note
pattern needs `note(...)` or `n(...)`: `.add(note(12))`.

**Not implemented**, and what to use instead where there is something:

| | |
|---|---|
| Effects | `duck`, `delay` (use `echo`), `room`/reverb, `lpf`/`hpf`/`vowel` and the other filters, `pan`, `crush`, `coarse`, `shape`, `distort`, `phaser` |
| Envelopes and length | `attack`/`decay`/`sustain`/`release`, `legato`, `clip`, `velocity` (use `gain`) |
| Samples | `speed`, `begin`/`end`, `chop`, `striate`, `slice`, `splice`, `loopAt`, `fit` (the OPL2 has no samples) |
| Time | `hurry`, `iter`, `palindrome`, `chunk`, `inside`/`outside`, `segment`, `range`, `swingBy`, `compress`, `zoom` |
| Combining | `jux`, `superimpose` (use `stack` or `off(0, ...)`), `layer`, `arrange`, `pick`, `squeeze` |
| Harmony | `arp`, `chord`, `voicing`, `rootNotes` |
| Signals and random | `sine`, `saw`, `square`, `tri`, `perlin`, `rand`, `irand`, `choose`, `wchoose` (as patterns: here `sine` and `square` are sound names) |
| Mini-notation | `..` ranges |
| Visuals | `pianoroll`, `punchcard`, `scope`, `spiral`, `_pianoroll()` and the other inline visuals |

The visuals are too much for this version: the 6502 already spends most of
its time working out the notes, and the screen is the editor. The playing
tokens light up in the code instead (below), and the panel under the editor
shows each OPL2 voice.

### Sounds

All from RPTracker's OPL2 patches. Any sound can also be written as its
patch number, as RPTracker shows it: `s("#50")` is Square Lead.

Drums, each at a fixed note:

| Name | Sound | Note |
|---|---|---|
| `bd` | bass drum (RPTracker `#FD`) | C3 |
| `sd` | snare (`#FE`) | C3 |
| `hh` | closed hi-hat (`#FF`) | C3 |
| `oh` | open hi-hat (new patch, not yet tuned by ear) | C3 |
| `cr` | crash (new patch, not yet tuned by ear) | G2 |
| `rd` | ride (the hat, higher) | C4 |
| `sh` | shaker (the hat, higher) | C5 |
| `tb` | tambourine (the hat) | G4 |
| `cp` | clap (the snare, higher) | G3 |
| `lt`, `mt`, `ht` | low, mid, high tom (Melodic Tom `#75`) | A2, D3, G3 |
| `rim` | rimshot (Woodblock `#73`) | E5 |
| `cb` | cowbell (Agogo `#71`) | G#4 |
| `perc` | percussion (Synth Drum `#76`) | C4 |

`#FD`, `#FE` and `#FF` play the kit's bass drum, snare and hat at C3.

Strudel's synths, as the nearest General MIDI patch (`n` is the note, as
in Strudel):

| Name | Patch |
|---|---|
| `sine` | `#4F` Ocarina |
| `triangle` | `#49` Flute (the default sound) |
| `square` | `#50` Square Lead |
| `sawtooth` | `#51` Sawtooth Lead |

<details>
<summary>The General MIDI sounds, by Strudel's names and patch numbers</summary>

| | | |
|---|---|---|
| `#00` `gm_piano` | `#2B` `gm_contrabass` | `#56` `gm_lead_7_fifths` |
| `#01` (Piano 2) | `#2C` `gm_tremolo_strings` | `#57` `gm_lead_8_bass_lead` |
| `#02` (Piano 3) | `#2D` `gm_pizzicato_strings` | `#58` `gm_pad_new_age` |
| `#03` (Honky-tonk) | `#2E` `gm_orchestral_harp` | `#59` `gm_pad_warm` |
| `#04` `gm_epiano1` | `#2F` `gm_timpani` | `#5A` `gm_pad_poly` |
| `#05` `gm_epiano2` | `#30` `gm_string_ensemble_1` | `#5B` `gm_pad_choir` |
| `#06` `gm_harpsichord` | `#31` `gm_string_ensemble_2` | `#5C` `gm_pad_bowed` |
| `#07` `gm_clavinet` | `#32` `gm_synth_strings_1` | `#5D` `gm_pad_metallic` |
| `#08` `gm_celesta` | `#33` `gm_synth_strings_2` | `#5E` `gm_pad_halo` |
| `#09` `gm_glockenspiel` | `#34` `gm_choir_aahs` | `#5F` `gm_pad_sweep` |
| `#0A` `gm_music_box` | `#35` `gm_voice_oohs` | `#60` `gm_fx_rain` |
| `#0B` `gm_vibraphone` | `#36` `gm_synth_choir` | `#61` `gm_fx_soundtrack` |
| `#0C` `gm_marimba` | `#37` `gm_orchestra_hit` | `#62` `gm_fx_crystal` |
| `#0D` `gm_xylophone` | `#38` `gm_trumpet` | `#63` `gm_fx_atmosphere` |
| `#0E` `gm_tubular_bells` | `#39` `gm_trombone` | `#64` `gm_fx_brightness` |
| `#0F` `gm_dulcimer` | `#3A` `gm_tuba` | `#65` `gm_fx_goblins` |
| `#10` `gm_drawbar_organ` | `#3B` `gm_muted_trumpet` | `#66` `gm_fx_echoes` |
| `#11` `gm_percussive_organ` | `#3C` `gm_french_horn` | `#67` `gm_fx_sci_fi` |
| `#12` `gm_rock_organ` | `#3D` `gm_brass_section` | `#68` `gm_sitar` |
| `#13` `gm_church_organ` | `#3E` `gm_synth_brass_1` | `#69` `gm_banjo` |
| `#14` `gm_reed_organ` | `#3F` `gm_synth_brass_2` | `#6A` `gm_shamisen` |
| `#15` `gm_accordion` | `#40` `gm_soprano_sax` | `#6B` `gm_koto` |
| `#16` `gm_harmonica` | `#41` `gm_alto_sax` | `#6C` `gm_kalimba` |
| `#17` `gm_bandoneon` | `#42` `gm_tenor_sax` | `#6D` `gm_bagpipe` |
| `#18` `gm_acoustic_guitar_nylon` | `#43` `gm_baritone_sax` | `#6E` `gm_fiddle` |
| `#19` `gm_acoustic_guitar_steel` | `#44` `gm_oboe` | `#6F` `gm_shanai` |
| `#1A` `gm_electric_guitar_jazz` | `#45` `gm_english_horn` | `#70` `gm_tinkle_bell` |
| `#1B` `gm_electric_guitar_clean` | `#46` `gm_bassoon` | `#71` `gm_agogo` |
| `#1C` `gm_electric_guitar_muted` | `#47` `gm_clarinet` | `#72` `gm_steel_drums` |
| `#1D` `gm_overdriven_guitar` | `#48` `gm_piccolo` | `#73` `gm_woodblock` |
| `#1E` `gm_distortion_guitar` | `#49` `gm_flute` | `#74` `gm_taiko_drum` |
| `#1F` `gm_guitar_harmonics` | `#4A` `gm_recorder` | `#75` `gm_melodic_tom` |
| `#20` `gm_acoustic_bass` | `#4B` `gm_pan_flute` | `#76` `gm_synth_drum` |
| `#21` `gm_electric_bass_finger` | `#4C` `gm_blown_bottle` | `#77` `gm_reverse_cymbal` |
| `#22` `gm_electric_bass_pick` | `#4D` `gm_shakuhachi` | `#78` `gm_guitar_fret_noise` |
| `#23` `gm_fretless_bass` | `#4E` `gm_whistle` | `#79` `gm_breath_noise` |
| `#24` `gm_slap_bass_1` | `#4F` `gm_ocarina` | `#7A` `gm_seashore` |
| `#25` `gm_slap_bass_2` | `#50` `gm_lead_1_square` | `#7B` `gm_bird_tweet` |
| `#26` `gm_synth_bass_1` | `#51` `gm_lead_2_sawtooth` | `#7C` `gm_telephone` |
| `#27` `gm_synth_bass_2` | `#52` `gm_lead_3_calliope` | `#7D` `gm_helicopter` |
| `#28` `gm_violin` | `#53` `gm_lead_4_chiff` | `#7E` `gm_applause` |
| `#29` `gm_viola` | `#54` `gm_lead_5_charang` | `#7F` `gm_gunshot` |
| `#2A` `gm_cello` | `#55` `gm_lead_6_voice` |  |

</details>

Each playing token lights up in the code, as in Strudel, and the
highlights follow the text as you edit.

### How it matches Strudel

The engine mirrors Strudel's own building blocks (`pure`, `stack`,
`slowcat`, `fast`, `compress`/`fastGap`, `repeatCycles`, `struct`,
`appLeft`, `squeezeJoin`...) with exact fractions. `tests/ref` runs
Strudel itself (from npm) to make the expected results, and the host tests
check that RPStrudel gives the same events for 78 mini-notation cases and
63 program cases.

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
  buffer), the clipboard and the undo history (4K each), the patterns (two banks, so a new program is parsed beside the
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
| Ctrl+Home, Ctrl+End | To the start, the end of the code |
| Shift with any of those | Select |
| Ctrl+A | Select all |
| Ctrl+C, Ctrl+X, Ctrl+V | Copy, cut, paste (up to 4K) |
| Backspace, Delete, Tab, Enter | Edit (Enter keeps the indent); typing replaces a selection |
| `(` `[` `{` `<` `"` | Also type their closer; typing the closer steps over it, and Backspace between an empty pair deletes both |
| Ctrl+Z | Undo (the last 4K or so of changes; no redo) |
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
./tools/rp6502-emu build/llvm-mos/release/RPStrudel.rp6502 --mute --script tests/clipboard.script
```

`tests/editor.script` types code, presses Ctrl+Enter, and checks the
screen through Ctrl+D: an update, a mistake that leaves the old code
playing, a fix with the editing keys, an unknown sound, and Esc.
`tests/keys.script` sends arrow sequences split across frames (as real
hardware can) and holds an arrow key down, checking that no cycle is late.
`tests/functions.script` types code with the milestone 4 functions, and an
unknown scale. `tests/clipboard.script` selects, copies, cuts, pastes and
undoes.

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
