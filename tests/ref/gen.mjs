// Runs every case in ../mini_cases.txt through Strudel's own mini-notation
// engine and writes the onsets of cycles 0-3 to ../mini_expected.txt.
//
//   cd tests/ref && npm install && node --import ./stub.mjs gen.mjs
import { readFileSync, writeFileSync } from 'node:fs';
import { mini } from '@strudel/mini';
import * as core from '@strudel/core';
import '@strudel/tonal';

const here = new URL('.', import.meta.url);
const cases = readFileSync(new URL('../mini_cases.txt', here), 'utf8')
  .split('\n')
  .map((l) => l.trim())
  .filter((l) => l && !l.startsWith('#'));

const frac = (f) => (f.d == 1 ? `${f.s * f.n}` : `${f.s * f.n}/${f.d}`);
const out = [];
for (const c of cases) {
  out.push(`= ${c}`);
  const pat = mini(c);
  for (let cyc = 0; cyc < 4; cyc++) {
    const lines = pat
      .queryArc(cyc, cyc + 1)
      .filter((h) => h.hasOnset())
      .map((h) => ({ b: h.whole.begin, line: `${cyc} ${frac(h.whole.begin)} ${frac(h.whole.end)} ${h.value}` }))
      .sort((x, y) => x.b.compare(y.b) || (x.line < y.line ? -1 : x.line > y.line ? 1 : 0));
    for (const l of lines) out.push(l.line);
  }
}
writeFileSync(new URL('../mini_expected.txt', here), out.join('\n') + '\n');
console.log(`${cases.length} mini cases`);

// Program expressions: strings become mini-notation, as Strudel's
// transpiler does, then the expression runs with the controls in scope.
const progCases = readFileSync(new URL('../prog_cases.txt', here), 'utf8')
  .split('\n')
  .map((l) => l.trim())
  .filter((l) => l && !l.startsWith('#'));
const names = ['s', 'sound', 'note', 'n', 'gain', 'stack', 'cat', 'seq', 'slowcat', 'fastcat'];
// note and n as MIDI numbers, gain to the 64ths RPStrudel keeps
const fmt = {
  s: (v) => v,
  note: (v) => (typeof v === 'string' ? core.noteToMidi(v) : Math.round(v)),
  n: (v) => Math.round(v),
  gain: (v) => Math.min(254, Math.floor(v * 64)) / 64,
};
const keys = Object.keys(fmt);
const plain = (v) => (typeof v === 'string' && core.tokenizeNote(v).length && /\d/.test(v) ? core.noteToMidi(v) : v);
const progOut = [];
for (const c of progCases) {
  progOut.push(`= ${c}`);
  const js = c.replace(/"([^"]*)"/g, (_, m) => `m(${JSON.stringify(m)})`);
  // every Strudel function in scope, as in the REPL
  const pat = new Function('m', 'core', `with (core) { return ${js}; }`)(mini, core);
  for (let cyc = 0; cyc < 4; cyc++) {
    const lines = pat
      .queryArc(cyc, cyc + 1)
      .filter((h) => h.hasOnset())
      .map((h) => {
        const v = h.value;
        const fields =
          typeof v === 'object'
            ? keys.filter((k) => v[k] !== undefined).map((k) => ` ${k}=${fmt[k](v[k])}`).join('')
            : ` ${plain(v)}`;
        return { b: h.whole.begin, line: `${cyc} ${frac(h.whole.begin)} ${frac(h.whole.end)}${fields}` };
      })
      .sort((x, y) => x.b.compare(y.b) || (x.line < y.line ? -1 : x.line > y.line ? 1 : 0));
    for (const l of lines) progOut.push(l.line);
  }
}
writeFileSync(new URL('../prog_expected.txt', here), progOut.join('\n') + '\n');
console.log(`${progCases.length} program cases`);
