// node web/dump.mjs FILE: the conformance dump (tests/README.md) from the
// WebAssembly build, so the suite can check the engine the playground uses:
//   python3 tests/run.py -- node web/dump.mjs
import { readFileSync } from "node:fs";
import Modelita from "./site/modelita.js";

const engine = await Modelita();
const results = JSON.parse(engine.ccall("modelita_run", "string", ["string"], [readFileSync(process.argv[2], "utf8")]));
const out = [];
for (const r of results) {
  out.push(`line ${r.line}`);
  if (r.kind === "error") { out.push(`error ${r.category}`, "end"); console.error(`line ${r.line}: ${r.message}`); continue; }
  if (r.kind === "operator") { out.push("operator", "end"); continue; }
  out.push("shape", `vertices ${r.vertices.length / 3}`);
  for (let i = 0; i < r.vertices.length; i += 3) out.push(`v ${r.vertices[i]} ${r.vertices[i + 1]} ${r.vertices[i + 2]}`);
  out.push(`faces ${r.faces.length}`);
  r.faces.forEach((f, i) => out.push(`f ${r.roles[i]} ${r.steps[i]} ${r.labels[i].join(",") || "-"} : ${f.join(" ")}`));
  out.push(`skipped ${r.skipped}`, "end");
}
console.log(out.join("\n"));
