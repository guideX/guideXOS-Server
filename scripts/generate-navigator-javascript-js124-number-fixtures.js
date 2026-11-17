#!/usr/bin/env node
'use strict';

// Generate independent ECMAScript Number-to-string fixtures with Node's
// Number implementation. This generator does not load or call Navigator code.
const fs = require('node:fs');
const path = require('node:path');

const output = process.argv[2] || path.join(
  'tests', 'navigator_javascript_js124_number_expected.h');
const data = new DataView(new ArrayBuffer(8));
const fixtures = new Map();

function addBits(bits) {
  bits = BigInt.asUintN(64, bits);
  data.setBigUint64(0, bits, true);
  const value = data.getFloat64(0, true);
  const expected = JSON.stringify(value);
  fixtures.set(bits.toString(16).padStart(16, '0'), expected);
}

function bitsOf(value) {
  data.setFloat64(0, value, true);
  return data.getBigUint64(0, true);
}

function addNeighbors(value) {
  const bits = bitsOf(value);
  addBits(bits - 1n);
  addBits(bits);
  addBits(bits + 1n);
}

for (const bits of [
  0x0000000000000000n, 0x8000000000000000n,
  0x0000000000000001n, 0x000fffffffffffffn,
  0x0010000000000000n, 0x7fefffffffffffffn,
  0xffefffffffffffffn, 0x4340000000000000n,
  0x3ff0000000000000n, 0xbff0000000000000n,
  0x7ff0000000000000n, 0xfff0000000000000n,
  0x7ff8000000000001n,
]) addBits(bits);

for (const value of [
  0.1, 0.3, 1.5, Math.PI, Number.MIN_VALUE,
  Number.MIN_NORMAL || 2.2250738585072014e-308,
  1e-7, 1e-6, 1e20, 1e21, Number.MAX_VALUE,
  9007199254740991, 9007199254740992,
]) addNeighbors(value);

// Deterministic xorshift64* bit patterns stress rounding, exponent changes,
// and negative values without relying on the Navigator formatter.
let state = 0x9e3779b97f4a7c15n;
for (let index = 0; index < 512; ++index) {
  state ^= state << 13n;
  state ^= state >> 7n;
  state ^= state << 17n;
  state = BigInt.asUintN(64, state);
  addBits(state);
}

const rows = [...fixtures.entries()].map(([bits, expected]) =>
  `    {0x${bits}ull, "${expected}"},`);
const header = [
  '#pragma once',
  '// Generated independently with Node v24.22.1 JSON.stringify.',
  '#include <cstddef>',
  '#include <cstdint>',
  'namespace gxos { namespace javascript {',
  'struct Js124NumberExpectedFixture {',
  '    std::uint64_t bits;',
  '    const char* expected;',
  '};',
  'static const Js124NumberExpectedFixture kJs124NumberExpectedFixtures[] = {',
  ...rows,
  '};',
  'static const std::size_t kJs124NumberExpectedFixtureCount =',
  '    sizeof(kJs124NumberExpectedFixtures) /',
  '    sizeof(kJs124NumberExpectedFixtures[0]);',
  '} } // namespace gxos::javascript',
  '',
].join('\n');
fs.writeFileSync(output, header, 'utf8');
console.log(`wrote ${fixtures.size} Node JSON.stringify fixtures to ${output}`);
