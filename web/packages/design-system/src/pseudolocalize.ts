/** Expand dictionary literals for layout testing, preserving IDs and user values at the call site. */
export function pseudolocalize(text: string): string {
  return `[${text.replace(/[aeiouаеёиоуыэюя]/giu, "$&$&")}]`;
}
