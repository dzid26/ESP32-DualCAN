/** A source span with the 1-based location of its first character. */
export interface Span {
  /** UTF-16 offset of the first character, inclusive. */
  start: number;
  /** UTF-16 offset after the last character, exclusive. */
  end: number;
  /** 1-based line of the first character. */
  line: number;
  /** 1-based column of the first character. */
  column: number;
}

/** One preprocessing diagnostic. Severity comes from the result bucket. */
export interface Diagnostic {
  /** Stable identifier for tests and future UI filtering. */
  code: string;
  /** Human-readable text without the location. */
  message: string;
  /** Where the problem is in the original source. */
  span: Span;
}

/** Render a diagnostic as `line:column: message`. */
export function formatDiagnostic(diagnostic: Diagnostic): string {
  return `${diagnostic.span.line}:${diagnostic.span.column}: ${diagnostic.message}`;
}
