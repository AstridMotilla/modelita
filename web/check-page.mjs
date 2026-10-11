// node web/check-page.mjs: the site's own logic (site/lang.js) against the
// real engine. Every example runs without errors, every step of every word
// builds, programs survive the trip through a link, and every figure and
// playground link on every page builds a shape.
import { readFileSync, readdirSync } from "node:fs";
import Modelita from "./site/modelita.js";
import { EXAMPLES, classify, tokenize, buildSteps, encode, decode } from "./site/lang.js";

const engine = await Modelita();
const run = text => JSON.parse(engine.ccall("modelita_run", "string", ["string"], [text]));
let failures = 0;
const check = (ok, what) => { if (!ok) { failures++; console.log("FAIL", what); } };

check(JSON.stringify(tokenize("k[top](0.8) storey(1.2, 3)^3  x[a | b]{roof} C")) ===
      JSON.stringify(["k[top](0.8)", "storey(1.2, 3)^3", "x[a | b]{roof}", "C"]), "tokenize keeps brackets together");
{
  const steps = buildSteps(run, [], tokenize("x[top] C + move(3) T"), "");
  check(JSON.stringify(steps.map(s => s.token)) === JSON.stringify(["T", "move(3)", "x[top] C +"]) &&
        steps.every(s => s.result.kind === "shape"), "steps of a word with +: the last term step by step, then each term whole");
}
check(classify("storey(h, r) = x(h) ; c").kind === "define" && classify("storey(h, r) = x(h)").params.length === 2, "classify a definition");
check(classify('export "a.obj" x C').word === "x C", "classify an export");
check(classify("  ; only a comment").kind === "blank", "classify a comment");

let words = 0;
for (const [name, text] of Object.entries(EXAMPLES)) {
  const errors = run(text).filter(r => r.kind === "error");
  check(errors.length === 0, `${name}: ${errors.map(e => `line ${e.line}: ${e.message}`).join("; ")}`);
  const lines = text.split("\n");
  lines.forEach((line, i) => {
    const c = classify(line);
    if (c.kind !== "show" && !(c.kind === "define" && !c.params.length)) return;
    const base = lines.slice(0, i).map(l => (classify(l).kind === "show" ? "" : l));
    let steps = buildSteps(run, base, tokenize(c.word), "");
    if (steps.at(-1).result.kind === "operator") steps = buildSteps(run, base, tokenize(c.word), " x[+z] C");
    for (const s of steps) check(s.result?.kind === "shape", `${name} line ${i + 1}, step ${s.token}: ${s.result?.kind}`);
    words++;
  });
}

for (const text of [...Object.values(EXAMPLES), "", "C", "x[+z] C ; ünïcödé {}#&?=", "x".repeat(5000)]) {
  const code = await encode(text);
  check(/^[A-Za-z0-9._~-]+$/.test(code), "link codes use only characters a #anchor keeps");
  check((await decode(code)) === text, `link round trip of ${JSON.stringify(text.slice(0, 30))}`);
}

// The figures (data-word) and playground links (data-play) written into the pages.
const unescape = s => s.replace(/&#10;/g, "\n").replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, '"').replace(/&amp;/g, "&");
const pages = readdirSync(new URL("./site/", import.meta.url), { recursive: true }).filter(f => f.endsWith(".html"));
let figures = 0;
for (const page of pages) {
  const html = readFileSync(new URL("./site/" + page, import.meta.url), "utf8");
  for (const [, attr, value] of html.matchAll(/data-(word|play)="([^"]*)"/g)) {
    const results = run(unescape(value));
    const last = results.at(-1);
    check(last?.kind === "shape" && results.every(r => r.kind !== "error"),
          `${page}: data-${attr}="${value}": ${results.filter(r => r.kind === "error").map(r => r.message).join("; ") || last?.kind}`);
    figures++;
  }
}

console.log(failures ? `${failures} page checks failed` : `page checks pass (${words} words, every step; ${figures} figures and links on ${pages.length} pages)`);
process.exit(failures ? 1 : 0);
