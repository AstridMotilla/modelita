// What the playground knows about Modelita text, apart from the engine:
// what kind of statement a line is, how a word splits into tokens, the
// steps of a word, and how a program travels in a link. app.js draws;
// check-page.mjs tests this file in Node.

// --- examples --------------------------------------------------------------------------
export const EXAMPLES = {
  "Tower": `; Read each word right to left: the seed comes last.
; Put the cursor on a line to see its shape.

storey(h) = x[inner](h) i[top](0.3)    ; a reusable step
roof = k[top](0.8)

tower = roof storey(1.2)^3 x[+z]{tower} C
x[tower&side](0.2) tower               ; push out every tower wall
house = k[top](0.9) x[+z](0.6) C(3, 2, 1)
cup = x[inner](-1.6) i[+z](0.2) C`,
  "Conway": `; Conway's operators, with no selector, act on the whole shape.
t I          ; truncated icosahedron: a football
s C          ; snub cube
b D          ; bevelled dodecahedron
d k T        ; triakis tetrahedron, then its dual

; With a selector they act on part of it.
d[top] x[+z] C
g[+z] C
e[+z|+x|+y] C`,
  "Labels": `; A label names what a step makes, and faces built from it keep it.
body = x[+z](0.4){roof} C(3, 2, 1)
i[roof&top](0.3) body
x[roof&inner](0.5) i[roof&top](0.3) body
x[~roof](0.15) body                     ; the faces bordering the roof`,
  "Handles": `; Bridge two faces with a tube to make a hole.
ring = h[inner] i[+x|-x](0.3) x[+x](1) x[-x](1) C
ring
k[top](0.6) x[+z](0.5) ring
h[inner] i[+z|-z](0.35) P6`,
};
export const FIRST_LINE = { "Tower": 6 };

// --- reading lines (only to know what each line is; the engine does the rest) ------------
export function classify(line) {
  const code = line.split(";")[0];
  if (!code.trim()) return { kind: "blank" };
  if (/^\s*(use|gallery)\b/.test(code)) return { kind: "directive" };
  let m = code.match(/^\s*export\s+"[^"]*"(.*)$/);
  if (m) return { kind: "show", word: m[1].trim() };
  m = code.match(/^\s*([A-Za-z][A-Za-z0-9_-]*)\s*(?:\(([^)]*)\))?\s*=(.*)$/);
  if (m) return { kind: "define", name: m[1], params: m[2] && m[2].trim() ? m[2].split(",").map(s => s.trim()) : [], word: m[3].trim() };
  return { kind: "show", word: code.trim() };
}

export function tokenize(word) {
  // Tokens are separated by spaces outside [ ], ( ) and { }.
  const out = [];
  let depth = 0, current = "";
  for (const ch of word) {
    if ("[({".includes(ch)) depth++;
    if ("])}".includes(ch)) depth = Math.max(0, depth - 1);
    if (/\s/.test(ch) && depth === 0) { if (current) out.push(current); current = ""; }
    else current += ch;
  }
  if (current) out.push(current);
  return out;
}

export function buildSteps(run, base, tokens, seed) {
  // Line k after the base: the last k tokens, i.e. the shape after k steps.
  const words = tokens.map((_, k) => tokens.slice(tokens.length - 1 - k).join(" ") + seed);
  const results = run(base.concat(words).join("\n"));
  const at = new Map(results.map(r => [r.line, r]));
  return words.map((w, k) => ({ token: tokens[tokens.length - 1 - k], result: at.get(base.length + 1 + k) }));
}

// The program travels in the link: deflated, then base64url, which an
// artifact link passes through as a plain #anchor.
export async function encode(text) {
  let bytes = new TextEncoder().encode(text), tag = "t";
  if (globalThis.CompressionStream) {
    bytes = new Uint8Array(await new Response(new Blob([bytes]).stream().pipeThrough(new CompressionStream("deflate-raw"))).arrayBuffer());
    tag = "z";
  }
  let bin = "";
  for (const b of bytes) bin += String.fromCharCode(b);
  return tag + btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}
export async function decode(code) {
  const b64 = code.slice(1).replace(/-/g, "+").replace(/_/g, "/");
  const bytes = Uint8Array.from(atob(b64 + "===".slice((b64.length + 3) % 4)), c => c.charCodeAt(0));
  if (code[0] === "z") return new Response(new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate-raw"))).text();
  return new TextDecoder().decode(bytes);
}
