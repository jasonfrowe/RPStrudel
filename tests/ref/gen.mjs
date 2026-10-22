// Runs every case in ../mini_cases.txt through Strudel's own mini-notation
// engine and writes the onsets of cycles 0-3 to ../mini_expected.txt.
//
//   cd tests/ref && npm install && node --import ./stub.mjs gen.mjs
import { readFileSync, writeFileSync } from 'node:fs';
import { mini } from '@strudel/mini';
import * as core from '@strudel/core';

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
const names = ['s', 'sound', 'note', 'n', 'gain'];
const keys = ['s', 'note', 'n', 'gain'];
const progOut = [];
for (const c of progCases) {
  progOut.push(`= ${c}`);
  const js = c.replace(/"([^"]*)"/g, (_, m) => `m(${JSON.stringify(m)})`);
  const pat = new Function('m', ...names, `return ${js};`)(mini, ...names.map((k) => core[k]));
  for (let cyc = 0; cyc < 4; cyc++) {
    const lines = pat
      .queryArc(cyc, cyc + 1)
      .filter((h) => h.hasOnset())
      .map((h) => {
        const v = h.value;
        const fields = keys.filter((k) => v[k] !== undefined).map((k) => ` ${k}=${v[k]}`).join('');
        return { b: h.whole.begin, line: `${cyc} ${frac(h.whole.begin)} ${frac(h.whole.end)}${fields}` };
      })
      .sort((x, y) => x.b.compare(y.b) || (x.line < y.line ? -1 : x.line > y.line ? 1 : 0));
    for (const l of lines) progOut.push(l.line);
  }
}
writeFileSync(new URL('../prog_expected.txt', here), progOut.join('\n') + '\n');
console.log(`${progCases.length} program cases`);
