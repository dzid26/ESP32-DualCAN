/**
 * Bundled Berry script examples — sourced from firmware/scripts_examples/.
 * Vite resolves the glob at build time and inlines the file contents,
 * so no runtime fetch is needed.
 */

const raw = import.meta.glob('../../firmware/scripts_examples/*.be', {
  query: '?raw',
  import: 'default',
  eager: true,
}) as Record<string, string>;

export interface Example {
  filename: string;     // basename, e.g. "tiles_demo.be"
  name: string;         // from `# @name` header, fallback to filename
  description: string;  // from `# @description` header (multi-line @-continuation supported)
  code: string;
}

function parseHeader(code: string): { name?: string; description?: string } {
  const lines = code.split('\n');
  let name: string | undefined;
  const descLines: string[] = [];
  let inDesc = false;

  for (const line of lines) {
    const m = line.match(/^#\s*(.*)$/);
    if (!m) break;                       // first non-comment line — header is over
    const body = m[1];
    const tag = body.match(/^@(\w+)\s+(.*)$/);
    if (tag) {
      inDesc = false;
      if (tag[1] === 'name') name = tag[2].trim();
      else if (tag[1] === 'description') { descLines.push(tag[2].trim()); inDesc = true; }
    } else if (inDesc) {
      descLines.push(body.trim());      // continuation of @description
    }
  }
  return { name, description: descLines.join(' ').trim() || undefined };
}

export const examples: Example[] = Object.entries(raw)
  .map(([path, code]) => {
    const filename = path.split('/').pop() ?? path;
    const { name, description } = parseHeader(code);
    return {
      filename,
      name: name ?? filename,
      description: description ?? '',
      code,
    };
  })
  .sort((a, b) => a.name.localeCompare(b.name));
