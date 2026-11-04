import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { preprocessScript, formatDiagnostic } from '../src/lib/preprocessor.ts';
import { REPO, loadDbcMessages, realScriptFiles } from './helpers/corpus.ts';
import {
  ORACLE_CACHE,
  PARSE_BIN,
  buildParseCheck,
  gccAvailable,
} from '../scripts/lexer-tools.ts';

function runParseCheck(files: string[]): void {
  const result = spawnSync(PARSE_BIN, files, { encoding: 'utf8' });
  const output = `${result.stdout ?? ''}${result.stderr ?? ''}`;
  assert.equal(result.status, 0, `parse_check failed:\n${output}`);
  assert(!/\bFAIL\b/.test(output), `parse_check reported a failure:\n${output}`);
}

test('generated scripts compile with the firmware parser', (t) => {
  if (!gccAvailable()) {
    if (process.env.CI) assert.fail('gcc is required for the parse_check oracle');
    t.skip('gcc not found; the compiler oracle runs in CI');
    return;
  }

  const messages = loadDbcMessages();
  const inputs = realScriptFiles();
  const generated: string[] = [];
  mkdirSync(ORACLE_CACHE, { recursive: true });

  function writeGenerated(name: string, result: ReturnType<typeof preprocessScript>): string {
    const path = join(ORACLE_CACHE, name);
    writeFileSync(path, result.code);
    generated.push(path);
    return path;
  }

  function writeResult(name: string, result: ReturnType<typeof preprocessScript>): string {
    assert.equal(
      result.errors.length,
      0,
      `${name}: ${result.errors.map(formatDiagnostic).join('; ')}`,
    );
    return writeGenerated(name, result);
  }

  for (const input of inputs) {
    const name = `${input.includes('test_scripts') ? 'test' : 'tesla'}__${input.split(/[\\/]/).pop()!.replace(/\.be$/, '.bep')}`;
    const result = preprocessScript(readFileSync(input, 'utf8'), messages);
    writeResult(name, result);
  }

  const valid = String.raw`#- block comment with ) and " -#
var d = can_msg_get(0, "DAS_bodyControls")
var v = msg_sig_get(d, "DAS_hazard" "LightRequest")
msg_sig_set(d, "DAS_hazardLightReques\x74", (int(msg_sig_get(d, "DAS_hazardLightRequest")) + 1) % 4)
var e = can_msg_new("DAS_bodyControls", 0)
print("msg_sig_get(d, 'GHOST')")
def can_msg_get(x)
  return x
end
var foo = {}
foo.can_msg_get(0, 1)
`;
  const validPath = join(ORACLE_CACHE, 'synthetic-valid.be');
  writeFileSync(validPath, valid);
  inputs.push(validPath);
  writeResult('synthetic-valid.bep', preprocessScript(valid, messages));

  const errorInput = 'var msg = {}\nvar bad = can_msg_get(0, "UNKNOWN_MESSAGE")\nvar y = 1 + msg_sig_get(msg, "GHOST_SIGNAL")\n';
  const errorPath = join(ORACLE_CACHE, 'synthetic-error.be');
  writeFileSync(errorPath, errorInput);
  inputs.push(errorPath);
  const errorResult = preprocessScript(errorInput, messages);
  assert(errorResult.errors.length > 0, 'the error fixture must report diagnostics');
  assert(errorResult.code.includes('assert(false, '), errorResult.code);
  writeGenerated('synthetic-error.bep', errorResult);

  const spacedInput = 'var bad = can_msg_get (0, "DAS_bodyControls")\n';
  const spacedPath = join(ORACLE_CACHE, 'synthetic-spaced.be');
  writeFileSync(spacedPath, spacedInput);
  const spacedResult = preprocessScript(spacedInput, messages);
  assert.equal(spacedResult.errors.length, 1, JSON.stringify(spacedResult.errors));
  assert(spacedResult.code.includes('assert(false, '), spacedResult.code);
  writeGenerated('synthetic-spaced.bep', spacedResult);

  buildParseCheck();
  runParseCheck(inputs);
  runParseCheck(generated);
});
