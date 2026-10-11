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
  "Mirror": `; Build half of a shape: mirror joins it to its reflection
; across the faces you pick, the seam.
half = x[+x&side](1.5) x[+z](2) C     ; a post and half a lintel
mirror[+x&top] half                   ; the seam is the lintel's end
k[top](0.5) mirror[+x&top] half       ; the copies keep their roles
mirror[-z] Y6                         ; a pyramid and its reflection: a diamond`,
  "Transforms": `; Transforms move vertices. Without a selector they move the whole shape;
; with one, only the picked faces' corners move, and the rest follows.
turn(30) C(3, 1, 1)                     ; turned about the vertical
stretch(1, 1, 2) I                      ; stretched upwards
turn[top](45) x[+z](3) C                ; a tower with its top twisted
stretch[top](0.3, 0.3) x[+z](2) C       ; tapered
move[top](1.5) x[+z](3) C               ; leaning
twist = turn[top](15) x[top](0.5)
twist^8 x[+z](0.5) C(1.4, 1.4, 0.4)     ; a twisted column, a storey at a time`,
  "Bodies": `; A shape can be in several pieces, its bodies. + puts shapes side by
; side, and array and spin make copies.
house = k[top](0.6) x[+z](0.8) C
tree = k[top](1.4) x[inner](0.2) i[+z](-3) P8(0.15, 0.8)
house + move(2.5, 1, -0.6) tree           ; a house and a tree
array(4, 3) house                         ; a row of houses
spin(8) move(4) house                     ; a ring of them, about the origin
turn(30) array(4, 3) house                ; each body turns where it stands
yard = house{home} + move(2.5, 1, -0.6) tree{tree}
move[tree](0, 0, 1) yard                  ; labels pick bodies`,
  "City": `; A small city, in one body. Lay out one block of four lots, mirror it
; into a grid of sixteen, then build: every lot of a kind is built by the
; same steps, because mirror's copies keep their labels.

; Four lots on a slab: the seed's top, and the tops of three extensions.
block = lift[+y](2){north} x[+x](2){east} C(2, 2, 0.2)
grid = mirror[-y] mirror[-x] block
lots = i[+z](0.2) grid                    ; each lot's rim is street

; What goes on each kind of lot
houses  = k[house&top](0.5) x[inner&north&!east](0.6){house}
offices = x[office&inner](0.4) i[office&top](0.15) x[inner&east&!north](1.4){office}
towers  = k[tower&top](0.4) x[tower&inner](1) i[tower&top](0.2) x[tower&inner](1.2) i[tower&top](0.15) x[inner&north&east](2.2){tower}
parks   = k[inner&!north&!east](0.15)

city = parks towers offices houses lots
city`,
  "City 2": `; A free city: separate buildings, placed with move, array and spin.
ground = C(14, 14, 0.2)
house = k[top](0.5) x[+z](0.6) C(1.2, 1, 0.6)
tower(h) = k[top](0.3) x[top](0.5) i[top](0.2) x[+z](h) C(1.2, 1.2, 0.4)
tree = k[top](1.2) x[inner](0.15) i[+z](-3) P8(0.12, 0.8)

row = array(5, 2.2) move(-4.4, -5.5, 0.4) house          ; a street of houses
ring = spin(6) move(3.2, 0, 0.3) tower(2.5)              ; towers around the square
centre = move(0, 0, 0.3) turn(45) tower(5)            ; and one in the middle, turned
trees = array(6, 0, 2) move(5.8, -5, 0.5) tree           ; a line of trees

city = ground + spin(2) row + ring + centre + spin(2) trees
city`,
};
export const FIRST_LINE = { "Tower": 6, "City": 16, "City 2": 12 };

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
  // In a word of several terms joined by +, the last term goes step by step,
  // and then each term to its left is added whole, as one step.
  const terms = [[]];
  for (const t of tokens) t === "+" ? terms.push([]) : terms.at(-1).push(t);
  const last = terms.at(-1);
  const steps = last.map((_, k) => ({ token: last[last.length - 1 - k], word: last.slice(last.length - 1 - k).join(" ") + seed }));
  for (let j = terms.length - 2; j >= 0; j--)
    steps.push({ token: terms[j].join(" ") + " +", word: terms.slice(j).map(t => t.join(" ")).join(" + ") + seed });
  const results = run(base.concat(steps.map(s => s.word)).join("\n"));
  const at = new Map(results.map(r => [r.line, r]));
  return steps.map((s, k) => ({ token: s.token, result: at.get(base.length + 1 + k) }));
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
