/**
 * Bundled Berry scripts — user scripts from scripts/tesla/, test scripts from firmware/test_scripts/.
 * Vite resolves the glob at build time and inlines the file contents,
 * so no runtime fetch is needed.
 */

const allRaw = import.meta.glob(['../../scripts/tesla/*.be', '../../firmware/test_scripts/*.be'], {
  query: '?raw',
  import: 'default',
  eager: true,
}) as Record<string, string>;

export interface Example {
  filename: string;     // basename, e.g. "tiles_demo.be"
  name: string;         // from `# @name` header, fallback to filename
  description: string;  // from `# @description` header (multi-line @-continuation supported)
  bus: number;          // from `# @bus` header
  code: string;
}

function parseHeader(code: string): { name?: string; description?: string; bus: number } {
  const lines = code.split(/\r?\n/);
  let name: string | undefined;
  const descLines: string[] = [];
  let inDesc = false;
  let bus = 0;

  for (const line of lines) {
    const m = line.match(/^#\s*(.*)$/);
    if (!m) break;
    const body = m[1];
    const tag = body.match(/^@(\w+)\s+(.*)$/);
    if (tag) {
      inDesc = false;
      if (tag[1] === 'name') name = tag[2].trim();
      else if (tag[1] === 'description') { descLines.push(tag[2].trim()); inDesc = true; }
      else if (tag[1] === 'bus') bus = parseInt(tag[2].trim(), 10) || 0;
    } else if (inDesc) {
      descLines.push(body.trim());
    }
  }
  return { name, description: descLines.join(' ').trim() || undefined, bus };
}

function toExamples(raw: Record<string, string>): Example[] {
  return Object.entries(raw)
    .map(([path, code]) => {
      const filename = path.split('/').pop() ?? path;
      const { name, description, bus } = parseHeader(code);
      return { filename, name: name ?? filename, description: description ?? '', bus, code };
    })
    .sort((a, b) => a.name.localeCompare(b.name));
}

// User scripts only (scripts/tesla/) — shown in the gallery
export const examples: Example[] = toExamples(
  Object.fromEntries(Object.entries(allRaw).filter(([k]) => k.includes('tesla/')))
);

// All scripts — shown in the editor dropdown
export const allExamples: Example[] = toExamples(allRaw);
