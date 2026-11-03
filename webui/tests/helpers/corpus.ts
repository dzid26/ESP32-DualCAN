import { readFileSync, readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseDbc, type Message } from '../../src/dbc/parser.ts';

export const REPO = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..');

export function loadDbcMessages(): Message[] {
  const messages: Message[] = [];
  for (const file of readdirSync(join(REPO, 'dbc')).filter(f => f.endsWith('.dbc')).sort()) {
    messages.push(...parseDbc(readFileSync(join(REPO, 'dbc', file), 'latin1')));
  }
  return messages;
}

export function realScriptFiles(): string[] {
  const files: string[] = [];
  for (const dir of ['firmware/test_scripts', 'scripts/tesla']) {
    for (const file of readdirSync(join(REPO, dir)).filter(f => f.endsWith('.be')).sort()) {
      files.push(join(REPO, dir, file));
    }
  }
  return files;
}
